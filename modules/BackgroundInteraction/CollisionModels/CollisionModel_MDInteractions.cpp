/***************************
 Ion Dynamics Simulation Framework (IDSimF)

 Copyright 2022 - Physical and Theoretical Chemistry /
 Institute of Pure and Applied Mass Spectrometry
 of the University of Wuppertal, Germany

 IDSimF is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 IDSimF is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with IDSimF.  If not, see <https://www.gnu.org/licenses/>.
 ****************************/

#include "CollisionModel_MDInteractions.hpp"
#include "Core_math.hpp"
#include "Core_utils.hpp"
#include "Core_randomGenerators.hpp"
#include <cmath>
#include <array>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <functional>
#include <initializer_list>

CollisionModel::MDInteractionsModel::MDInteractionsModel(double staticPressure,
                                                        double staticTemperature,
                                                        double collisionGasMassAmu,
                                                        double collisionGasDiameterM, 
                                                        double collisionGasPolarizabilityM3,
                                                        std::string collisionMolecule,
                                                        double integrationTime,
                                                        double subTimeStep) :
        MDInteractionsModel(
        getConstantDoubleFunction(staticPressure),
        getConstantVectorFunction(Core::Vector(0.0, 0.0, 0.0)),
        staticTemperature,
        collisionGasMassAmu,
        collisionGasDiameterM,
        collisionGasPolarizabilityM3,
        collisionMolecule,
        integrationTime,
        subTimeStep) { }

CollisionModel::MDInteractionsModel::MDInteractionsModel(std::function<double(Core::Vector& location)> pressureFunction,
                                                        std::function<Core::Vector(Core::Vector& location)> velocityFunction,
                                                        double staticTemperature,
                                                        double collisionGasMassAmu,
                                                        double collisionGasDiameterM, 
                                                        double collisionGasPolarizabilityM3,
                                                        std::string collisionMolecule,
                                                        double integrationTime,
                                                        double subTimeStep) :
        MDInteractionsModel(
                std::move(pressureFunction),
                std::move(velocityFunction),
                getConstantDoubleFunction(staticTemperature),
                collisionGasMassAmu,
                collisionGasDiameterM, 
                collisionGasPolarizabilityM3,
                collisionMolecule,
                integrationTime,
                subTimeStep) { }

CollisionModel::MDInteractionsModel::MDInteractionsModel(std::function<double(Core::Vector& location)> pressureFunction,
                                                        std::function<Core::Vector(Core::Vector& location)> velocityFunction,
                                                        std::function<double(const Core::Vector&)> temperatureFunction,
                                                        double collisionGasMassAmu,
                                                        double collisionGasDiameterM, 
                                                        double collisionGasPolarizabilityM3,
                                                        std::string collisionMolecule,
                                                        double integrationTime,
                                                        double subTimeStep) :

        collisionGasMass_kg_(collisionGasMassAmu*Core::AMU_TO_KG),
        collisionGasDiameter_m_(collisionGasDiameterM),
        collisionGasPolarizability_m3_(collisionGasPolarizabilityM3),
        collisionMolecule_(collisionMolecule),
        integrationTime_(integrationTime),
        subTimeStep_(subTimeStep),
        pressureFunction_(std::move(pressureFunction)),
        velocityFunction_(std::move(velocityFunction)),
        temperatureFunction_(std::move(temperatureFunction)) { }


double CollisionModel::MDInteractionsModel::calcSign(double value){
    if(value > 0){
        return 1.;
    }else if(value < 0){
        return -1.;
    }else{
        return 0;
    }
}

void CollisionModel::MDInteractionsModel::initializeModelParameters(Core::Particle& /*ion*/) const {

}

void CollisionModel::MDInteractionsModel::updateModelParameters(Core::Particle& /*ion*/) const {

}

void CollisionModel::MDInteractionsModel::modifyAcceleration(Core::Vector& /*acceleration*/, Core::Particle& /*particle*/,
                                                         double /*dt*/) {

}

void CollisionModel::MDInteractionsModel::modifyVelocity(Core::Particle& particle, double dt) {
    //std::cout << particle.getVelocity() << std::endl;
    //std::cout << particle.getVelocity() << std::endl;
    //std::cout <<"Vel1: " <<particle.getVelocity() << std::endl;

    Core::RandomSource* rndSource = Core::globalRandomGeneratorPool->getThreadRandomSource();

    // Calculate collision cross section between particle and collision gas: # 1.2 for 10Td
    double sigma_m2 = M_PI * std::pow( (1.20*particle.getDiameter() + collisionGasDiameter_m_)/2.0, 2.0);
    Core::Vector moleculeComPosition = particle.getLocation();
    double localPressure_Pa = pressureFunction_(moleculeComPosition);
    if (Core::isDoubleEqual(localPressure_Pa, 0.0)){
        return; //pressure 0 means no collision at all
    }

    // Transform the frame of reference in a frame where the mean background gas velocity is zero.
    Core::Vector vGasMean = velocityFunction_(moleculeComPosition);
    Core::Vector vFrameMeanBackRest = particle.getVelocity() - vGasMean;

    double vRelIonMeanBackRest = vFrameMeanBackRest.magnitude(); //relative ion relative to bulk gas velocity     

    // Calculate the mean free path (MFP) from current ion velocity:

    // a static ion leads in static gas leads to a relative velocity of zero, which leads
    // to undefined behavior due to division by zero later.
    // The whole process converges to the MFP and collision probability of a static ion, thus
    // it is possible to assume a small velocity (1 nm/s) for the static ions to get rid of undefined behavior
    if (vRelIonMeanBackRest < 1e-9){
        vRelIonMeanBackRest = 1e-9;
    }

    // Calculate the mean gas speed (m/s)
    double temperature_K = temperatureFunction_(moleculeComPosition);
    double vMeanGas = std::sqrt(8.0*Core::K_BOLTZMANN*temperature_K/M_PI/(collisionGasMass_kg_));

    // Calculate the median gas speed (m/s)
    double vMedianGas = std::sqrt(2.0*Core::K_BOLTZMANN*temperature_K/(collisionGasMass_kg_));

    // Compute the mean relative speed (m/s) between ion and gas.
    double s = vRelIonMeanBackRest / vMedianGas;
    double cMeanRel = vMeanGas * (
            (s + 1.0/(2.0*s)) * 0.5 * sqrt(M_PI) * std::erf(s) + 0.5 * std::exp(-s*s) );

    // Compute mean-free-path (m)
    double effectiveMFP_m = Core::K_BOLTZMANN * temperature_K *
                            (vRelIonMeanBackRest / cMeanRel) / (localPressure_Pa * sigma_m2);

    // Compute probability of collision in the current time-step.
    double collisionProb = 1.0 - std::exp(-vRelIonMeanBackRest * dt / effectiveMFP_m);
    
    // FIXME: The time step length dt is unrestricted
    // Possible mitigation: Throw warning / exception if collision probability becomes too high

    // Decide if a collision actually happens:
    if (rndSource->uniformRealRndValue() > collisionProb){
        return; // no collision takes place
    }     
    //std::cout << collisionProb << std::endl;      
    
    // Collision happens
    // Construct the actual molecule and its atoms 
    CollisionModel::Molecule mole = CollisionModel::Molecule(particle.getLocation(), particle.getVelocity(), particle.getMolecularStructure());
    //std::cout << mole.getAtoms().at(0)->getRelativePosition() << std::endl;

    // Construct the background gas particle 

    CollisionModel::Molecule bgMole = CollisionModel::Molecule(Core::Vector(0.0, 0.0, 0.0), Core::Vector(0.0, 0.0, 0.0), 
                                        CollisionModel::MolecularStructure::molecularStructureCollection.at(collisionMolecule_));

    // Give background gas its position, velocity, rotation:

    // put the collision gas in the half-sphere in front of the molecule 
    // TODO: change this to a circular plane 
    double phi = M_PI/2 - M_PI * rndSource->uniformRealRndValue(); 
    double theta = M_PI - M_PI * rndSource->uniformRealRndValue();
    //double theta = M_PI/2;
    // Core::Vector positionBgMolecule = {mole.getComPos().x() + 12e-10/*+ sin(theta) * cos(phi) * 1.5 * mole.getDiameter()*/,
    //                                     mole.getComPos().y() /*+ sin(phi) * sin(theta) * 1.5 * mole.getDiameter()*/,
    //                                     mole.getComPos().z() /*+ cos(theta) * 1.5 * mole.getDiameter()*/};
    double collisionRadius = (mole.getDiameter() + collisionGasDiameter_m_)/2.0; // 1.12 factor is ok
    Core::Vector positionBgMolecule = {mole.getComPos().x() + mole.getComVel().x() / mole.getComVel().magnitude() * 7.5e-10  + sin(theta) * cos(phi)  * 1* collisionRadius,
                                        mole.getComPos().y()  + mole.getComVel().y() / mole.getComVel().magnitude() * 7.5e-10   + sin(phi) * sin(theta)  * 1* collisionRadius,
                                        mole.getComPos().z()  + mole.getComVel().z() / mole.getComVel().magnitude() * 7.5e-10  + cos(theta) * 1* collisionRadius};
    
    // // put the collision gas in the half-sphere in front of the molecule 
    // // TODO: change this to a circular plane 
    // double collisionRadius = 1.2*(mole.getDiameter() + collisionGasDiameter_m_)/2.0;
    // double dist = 8.e-10;
    // double phi_max = asin(collisionRadius/dist);
    // double theta_s = acos(mole.getComVel().z() / dist);
    // double phi_s = atan2(mole.getComVel().y(), mole.getComVel().x());
    // double phi = phi_max * (rndSource->uniformRealRndValue()-.5) + phi_s; //stay
    // double theta = 2. * M_PI * rndSource->uniformRealRndValue() + theta_s;
    
    // Core::Vector positionBgMolecule = mole.getComPos() + Core::Vector{sin(theta) * cos(phi), sin(phi) * sin(theta), cos(theta)} * dist;
    
    bgMole.setComPos(positionBgMolecule);
    // Core::Vector positionBgMolecule = { mole.getComVel().x(),
    //                                          mole.getComVel().y(),
    //                                          mole.getComVel().z()};
    // positionBgMolecule = positionBgMolecule / positionBgMolecule.magnitude() * 1.5 * mole.getDiameter();
    // bgMole.setComPos(positionBgMolecule);


    // Calculate the standard deviation of the one dimensional velocity distribution of the
    // background gas particles. Std. dev. in one dimension is given from Maxwell-Boltzmann
    // as sqrt(kT / particle mass).
    double  vrStdevBgMolecule = std::sqrt( Core::K_BOLTZMANN * temperature_K / (collisionGasMass_kg_) );
    Core::Vector velocityBgMolecule = { rndSource->normalRealRndValue() * vrStdevBgMolecule, 
                                        rndSource->normalRealRndValue() * vrStdevBgMolecule,
                                        rndSource->normalRealRndValue() * vrStdevBgMolecule};
    // velocityBgMolecule = velocityBgMolecule * 0.;
    // bgMole.setComVel(velocityBgMolecule);
    double velocityMagnitudeBgMolecule = velocityBgMolecule.magnitude();
    //double velocityMagnitudeBgMolecule = rndSource->normalRealRndValue() * vrStdevBgMolecule;
    Core::Vector velocityToIonBgMolecule = { mole.getComVel().x() * -1,
                                             mole.getComVel().y() * -1,
                                             mole.getComVel().z() * -1};
    velocityToIonBgMolecule = velocityToIonBgMolecule / velocityToIonBgMolecule.magnitude() * velocityMagnitudeBgMolecule;
    //std::cout << velocityToIonBgMolecule << std::endl;
    //bgMole.setComVel(Core::Vector(0.0, 0.0, 0.0));
    bgMole.setComVel(velocityToIonBgMolecule);
    //std::cout << bgMole.getComVel() << std::endl;
    // std::cout << mole.getMass() << " "  << bgMole.getMass() << std::endl;

    // rotate it randomly 
    bgMole.setAngles(Core::Vector(rndSource->uniformRealRndValue(), 
                                  rndSource->uniformRealRndValue(), 
                                  rndSource->uniformRealRndValue()));

    // Give molecule a random orientation:
    mole.setAngles(Core::Vector(rndSource->uniformRealRndValue(), 
                                rndSource->uniformRealRndValue(), 
                                rndSource->uniformRealRndValue()));

    // Switch to COM frame 
    Core::Vector momentumSum = Core::Vector(0.0, 0.0, 0.0);
    Core::Vector positionSum = Core::Vector(0.0, 0.0, 0.0);
    double massSum = 0;
    std::vector<CollisionModel::Molecule*> moleculesPtr = {&mole, &bgMole};
    for(auto* molecule : moleculesPtr){
        momentumSum += molecule->getComVel() * molecule->getMass();
        positionSum += molecule->getComPos() * molecule->getMass();
        massSum += molecule->getMass();
    }
    for(auto* molecule : moleculesPtr){
        molecule->setComVel(molecule->getComVel() - (momentumSum / massSum));
        molecule->setComPos(molecule->getComPos() - (positionSum / massSum));
    }
    // std::cout <<"Vel1COM: " << mole.getComVel() << std::endl;
    // std::cout <<"Vel1COMBG: " << bgMole.getComVel() << std::endl;


    // Call the sub-integrator
    double finalTime = integrationTime_; //  final integration time in seconds
    double timeStep = subTimeStep_; // step size in seconds 
    leapfrogIntern(moleculesPtr, timeStep, finalTime);

    // std::cout <<"Vel2COM: " << mole.getComVel() << std::endl;
    // std::cout <<"Vel2COMBG: " << bgMole.getComVel() << std::endl;

    //reset to lab frame 
    for(auto* molecule : moleculesPtr){
        molecule->setComPos(molecule->getComPos() + (positionSum / massSum) + (momentumSum / massSum) * finalTime);
        molecule->setComVel(molecule->getComVel() + (momentumSum / massSum));
    }
    
    // set the velocity and position of the relevant Particle 
    particle.setVelocity(mole.getComVel());
    // std::cout <<"Vel2: " <<particle.getVelocity() << std::endl;
    // std::cout <<"Vel2BG: " << bgMole.getComVel() << std::endl;
    //std::cout << particle.getVelocity() << std::endl;


}

void CollisionModel::MDInteractionsModel::modifyPosition(Core::Vector& /*position*/, Core::Particle& /*particle*/, double /*dt*/) {

}

void CollisionModel::MDInteractionsModel::leapfrogIntern(std::vector<CollisionModel::Molecule*> moleculesPtr, double dt, double finalTime){

    std::ofstream positionOut;
    positionOut.open("position_output.txt");
    // std::ofstream velocityOut;
    // velocityOut.open("velocity_output.txt");
    
    int nSteps = int(round(finalTime/dt));
    //std::cout << nSteps << std::endl;
    //size_t nMolecules = moleculesPtr.size();

    std::vector<Core::Vector> forceMolecules = forceFieldMD(moleculesPtr);

    // do the first half step for the velocity, as per leapfrog definition
    size_t i = 0;
    for(auto* molecule : moleculesPtr){
        Core::Vector newComVel =  molecule->getComVel() + forceMolecules.at(i) / molecule->getMass() * dt/2;
        molecule->setComVel(newComVel);
        i++;
    }

    // start the actual leapfrog iteration
    for (int j = 0; j < nSteps; j++){
       
        // time step for the new position 
        i = 0;
        for(auto* molecule : moleculesPtr){
            // if(i == 0){
            //     positionOut << molecule->getComPos().magnitude() << std::endl;
            // }
            positionOut << molecule->getMass()/Core::AMU_TO_KG << ", " << molecule->getComPos().x() << ", " << molecule->getComPos().y() << ", " << molecule->getComPos().z() << std::endl;
            Core::Vector newComPos =  molecule->getComPos() + molecule->getComVel() * dt;
            molecule->setComPos(newComPos);
            i++;
        }

        // recalculate the force
        forceMolecules = forceFieldMD(moleculesPtr);  
        i = 0;
        // time step for the new velocity
        for(auto* molecule : moleculesPtr){
            // if(i == 0){
            //     velocityOut << molecule->getComVel().magnitude() << std::endl;
            // }
            Core::Vector newComVel =  molecule->getComVel() + forceMolecules.at(i) / molecule->getMass() * dt;
            // if(j%50){
            //     std::cout << i << " newVel: " <<newComVel << std::endl;
            // }
            
            molecule->setComVel(newComVel);
            i++;
        }
    }

}

void CollisionModel::MDInteractionsModel::rk4Intern(std::vector<CollisionModel::Molecule*> moleculesPtr, double dt, double finalTime){

    // std::ofstream positionOut;
    // positionOut.open("position_output_2.txt");
    // std::ofstream velocityOut;
    // velocityOut.open("velocity_output_2.txt");

    int nSteps = int(round(finalTime/dt));
    size_t nMolecules = moleculesPtr.size();
    std::vector<Core::Vector> forceMolecules(nMolecules);

    size_t i = 0;

    // start the actual leapfrog iteration
    for (int j = 0; j < nSteps; j++){
        
        std::vector<Core::Vector> velocityMolecules(nMolecules);
        i = 0;
        for(auto* molecule : moleculesPtr){
            velocityMolecules.at(i) = molecule->getComVel();
            i++;
        }
        std::vector<Core::Vector> positionMolecules(nMolecules);
        i = 0;
        for(auto* molecule : moleculesPtr){
            positionMolecules.at(i) = molecule->getComPos();
            i++;
        }
        std::vector<Core::Vector> initialPositionMolecules(nMolecules);
        for(size_t k = 0; k < nMolecules; k++){
            initialPositionMolecules.at(k) = Core::Vector( positionMolecules.at(k).x(), positionMolecules.at(k).y(),positionMolecules.at(k).z() );
        }
        std::vector<Core::Vector> initialVelocityMolecules(nMolecules);
        for(size_t k = 0; k < nMolecules; k++){
            initialVelocityMolecules.at(k) = Core::Vector( velocityMolecules.at(k).x(), velocityMolecules.at(k).y(), velocityMolecules.at(k).z() );
        }
        
        double length[3] = {1./2, 1./2, 1};
        double mass[nMolecules];
        i = 0;
        for(auto* molecule : moleculesPtr){
            mass[i] = molecule->getMass();
            i++;
        }
        std::vector<Core::Vector> forceMolecules = forceFieldMD(moleculesPtr);

        std::array<std::array<Core::Vector, 2>, 4> k; 
        std::array<std::array<Core::Vector, 2>, 4> l; 

    
        for(size_t q = 0; q < nMolecules; q++){
            k[0][q] = forceMolecules.at(q) * dt / mass[q];
            l[0][q] = velocityMolecules.at(q) * dt;
        }

        for(size_t n = 1; n < 4; n++){
            i = 0;
            for(auto* molecule : moleculesPtr){
                positionMolecules.at(i) = initialPositionMolecules.at(i) + l[n-1][i]*length[i-1];
                molecule->setComPos(positionMolecules.at(i));
                i++;
            }
            forceMolecules = forceFieldMD(moleculesPtr);

            i = 0;
            for(auto* molecule : moleculesPtr){
                k[n][i] = forceMolecules.at(i) * dt / mass[i];
                l[n][i] = (velocityMolecules.at(i) + k[n-1][i]*length[n-1])*dt;
                i++;
            }
            
        }

        i = 0;
        for(auto* molecule : moleculesPtr){
            // if(i == 0){
            //     positionOut << molecule->getComPos().magnitude() << std::endl;
            //     velocityOut << molecule->getComVel().magnitude() << std::endl;
            // }

            Core::Vector newComPos = initialPositionMolecules.at(i) + (l[0][i]+ l[1][i]*2 + l[2][i]*2 + l[3][i]) * 1./6; 
            molecule->setComPos(newComPos);
            Core::Vector newComVel = initialVelocityMolecules.at(i) + (k[0][i]+ k[1][i]*2 + k[2][i]*2 + k[3][i]) * 1./6; 
            molecule->setComVel(newComVel);
            //positionOut << molecule->getMass()/Core::AMU_TO_KG << ", " << molecule->getComPos().x() << ", " << molecule->getComPos().y() << ", " << molecule->getComPos().z() << std::endl;
            i++;
        }
    }
}


void CollisionModel::MDInteractionsModel::rk4InternAdaptiveStep(std::vector<CollisionModel::Molecule*> moleculesPtr, double dt, double finalTime){

    std::ofstream positionOut;
    positionOut.open("position_output.txt");
    // std::ofstream velocityOut;
    // velocityOut.open("velocity_output_2.txt");
    // std::ofstream timeOut;
    // timeOut.open("time_out.txt");

    int nSteps = int(round(finalTime/dt));
    double integrationTimeSum = 0;
    size_t nMolecules = moleculesPtr.size();
    std::vector<Core::Vector> forceMolecules(nMolecules);

    size_t i = 0;
    int steps = 0;
    

    // start the actual leapfrog iteration
    while(integrationTimeSum < finalTime){
        steps++;
        std::vector<Core::Vector> velocityMolecules(nMolecules);
        i = 0;
        for(auto* molecule : moleculesPtr){
            velocityMolecules.at(i) = molecule->getComVel();
            i++;
        }
        std::vector<Core::Vector> positionMolecules(nMolecules);
        i = 0;
        for(auto* molecule : moleculesPtr){
            positionMolecules.at(i) = molecule->getComPos();
            i++;
        }
        std::vector<Core::Vector> initialPositionMolecules(nMolecules);
        for(size_t k = 0; k < nMolecules; k++){
            initialPositionMolecules.at(k) = Core::Vector( positionMolecules.at(k).x(), positionMolecules.at(k).y(),positionMolecules.at(k).z() );
        }
        std::vector<Core::Vector> initialVelocityMolecules(nMolecules);
        for(size_t k = 0; k < nMolecules; k++){
            initialVelocityMolecules.at(k) = Core::Vector( velocityMolecules.at(k).x(), velocityMolecules.at(k).y(), velocityMolecules.at(k).z() );
        }
        
        //double length[3] = {1./2, 1./2, 1};
        double length[6][5] = { {1./4, 0, 0, 0, 0},
                                {3./32, 9./32, 0, 0, 0},
                                {1932./2197, -7200./2197, 7296./2197, 0, 0},
                                {439./216, -8, 3680./513, -645./4104, 0},
                                {-8./27, 2, -3544./2565, 1859./4104, -11./40}};
        double mass[nMolecules];
        i = 0;
        for(auto* molecule : moleculesPtr){
            mass[i] = molecule->getMass();
            i++;
        }
        std::vector<Core::Vector> forceMolecules = forceFieldMD(moleculesPtr);

        std::array<std::array<Core::Vector, 2>, 6> k; 
        std::array<std::array<Core::Vector, 2>, 6> l; 

    
        for(size_t q = 0; q < nMolecules; q++){
            k[0][q] = forceMolecules.at(q) * dt / mass[q];
            l[0][q] = velocityMolecules.at(q) * dt;
        }

        for(size_t n = 1; n < 6; n++){
            i = 0;
            for(auto* molecule : moleculesPtr){
                positionMolecules.at(i) = initialPositionMolecules.at(i);
                molecule->setComPos(positionMolecules.at(i));
                i++;
            }
            for(size_t m = 0; m < 5; m++){
                i = 0;
                for(auto* molecule : moleculesPtr){
                    positionMolecules.at(i) += l[n-1][i]*length[n-1][m];
                    molecule->setComPos(positionMolecules.at(i));
                    i++;
                }
            }
            forceMolecules = forceFieldMD(moleculesPtr);
            for(size_t m = 0; m < 5; m++){
                i = 0;
                for(auto* molecule : moleculesPtr){
                    k[n][i] = forceMolecules.at(i) * dt / mass[i];
                    l[n][i] = (velocityMolecules.at(i) + k[n-1][i]*length[n-1][m])*dt;
                    i++;
                }
            }
             
        }

        i = 0;
        for(auto* molecule : moleculesPtr){
            // if(i == 0){
            //     positionOut << molecule->getComPos().magnitude() << std::endl;
            //     velocityOut << molecule->getComVel().magnitude() << std::endl;
            // }
            positionOut << molecule->getMass()/Core::AMU_TO_KG << ", " << molecule->getComPos().x() << ", " << molecule->getComPos().y() << ", " << molecule->getComPos().z() << std::endl;
            Core::Vector newComPosOrder5 = initialPositionMolecules.at(i) + (l[0][i] * 16./135 + l[2][i] * 6656./12825 + l[3][i] * 28561./56430 + l[4][i] * (-9./50) + l[5][i] * 2./55); 
            Core::Vector newComVelOrder5 = initialVelocityMolecules.at(i) + (k[0][i] * 16./135 + k[2][i] * 6656./12825 + k[3][i] * 28561./56430 + k[4][i] * (-9./50) + k[5][i] * 2./55); 

            Core::Vector newComPosOrder4 = initialPositionMolecules.at(i) + (l[0][i] * 25./216 + l[2][i] * 1405./2565 + l[3][i] * 2197./4104 + l[4][i] * (-1./5)); 
            Core::Vector newComVelOrder4 = initialVelocityMolecules.at(i) + (k[0][i] * 25./216 + k[2][i] * 1405./2565 + k[3][i] * 2197./4104 + k[4][i] * (-1./5)); 

            double deltaX = fabs(newComVelOrder4.x() - newComVelOrder5.x())/ (fabs(newComVelOrder5.x()) * 2 * 2 * 2 * 2 - 1);
            double deltaY = fabs(newComVelOrder4.y() - newComVelOrder5.y())/ (fabs(newComVelOrder5.y()) * 2 * 2 * 2 * 2 - 1);
            double deltaZ = fabs(newComVelOrder4.z() - newComVelOrder5.z())/ (fabs(newComVelOrder5.z()) * 2 * 2 * 2 * 2 - 1);
            double globalDelta = std::max({deltaX, deltaY, deltaZ});
            integrationTimeSum += dt;
            //std::cout << integrationTimeSum << std::endl;
            double newdt = dt * std::pow((6e-6/globalDelta), 1./5) * 0.9;

            if(newdt >= 1e-19 && !std::isinf(newdt)){
                dt = newdt;
            } 

            //timeOut << dt << std::endl;
            molecule->setComPos(newComPosOrder4);
            molecule->setComVel(newComVelOrder4);

            //positionOut << molecule->getMass()/Core::AMU_TO_KG << ", " << molecule->getComPos().x() << ", " << molecule->getComPos().y() << ", " << molecule->getComPos().z() << std::endl;
            i++;
        }
    }
    //std::cout << steps << std::endl;
}


std::vector<Core::Vector> CollisionModel::MDInteractionsModel::forceFieldMD(std::vector<CollisionModel::Molecule*> moleculesPtr){

    size_t nMolecules = moleculesPtr.size();
    std::vector<Core::Vector> forceMolecules(nMolecules); // save all the forces acting on each molecule

    //std::cout << forceMolecules.at(0) << std::endl;
    // each molecule interacts with each other molecule
    for (size_t i = 0; i+1 < nMolecules; i++){
        for (size_t j = i+1; j < nMolecules; j++){
            /* 
            * therefore we need the interaction between each atom of a molecule with the atoms of the
            * other one 
            */ 

            // construct E-field acting on the molecule
            std::array<double, 3> eField = {0., 0., 0.};
            std::array<double, 6> eFieldDerivative = {0., 0., 0., 0., 0., 0.};

            for(auto& atomI : moleculesPtr.at(i)->getAtoms()){
                for(auto& atomJ : moleculesPtr.at(j)->getAtoms()){
                    
                    // First contribution: Lennard-Jones potential 
                    // This always contributes to the experienced force 
                    Core::Vector absPosAtomI = moleculesPtr.at(i)->getComPos() + atomI->getRelativePosition();
                    Core::Vector absPosAtomJ = moleculesPtr.at(j)->getComPos() + atomJ->getRelativePosition();


                    Core::Vector distance = absPosAtomI - absPosAtomJ;
                    if(distance.magnitude() < 1E-25){
                        forceMolecules.at(i) += Core::Vector(1e-10, 1e-10, 1e-10);
                        forceMolecules.at(j) += Core::Vector(1e-10, 1e-10, 1e-10) * (-1);
                        break;
                    }
                    if(distance.magnitude() > 1E20){
                        return forceMolecules;
                    }
                    double distanceSquared = distance.magnitudeSquared();
                    double distanceSquaredInverse = 1./distanceSquared;
                    double sigma = CollisionModel::Atom::calcLJSig(*atomI, *atomJ);
                    double sigma6 = sigma * sigma * sigma * sigma * sigma * sigma;
                    double epsilon = CollisionModel::Atom::calcLJEps(*atomI, *atomJ);
                    double ljFactor = 24 * epsilon * distanceSquaredInverse*distanceSquaredInverse*distanceSquaredInverse*distanceSquaredInverse * 
                                        (2 * distanceSquaredInverse*distanceSquaredInverse*distanceSquaredInverse * sigma6 * sigma6 - sigma6);
                    // calculate the force that acts on the atoms and add it to the overall force on the molecule
                    Core::Vector atomForce;
                    atomForce.x(distance.x() * ljFactor);
                    atomForce.y(distance.y() * ljFactor);
                    atomForce.z(distance.z() * ljFactor);
                    forceMolecules.at(i) += atomForce;
                    forceMolecules.at(j) += atomForce * (-1);

                    // Second contribution: C4 ion-induced dipole potential
                    // This requires an ion and one neutrally charged molecule to be present
                    double distanceCubed = distanceSquared * sqrt(distanceSquared);
                    double currentCharge = 0;
                    // Check if one of the molecules is an ion and the other one is not
                    if(int(atomI->getCharge()/Core::ELEMENTARY_CHARGE) != 0 && 
                        moleculesPtr.at(j)->getIsIon() == false &&
                        moleculesPtr.at(j)->getIsDipole() == false){
                        currentCharge = atomI->getCharge();

                    }else if (moleculesPtr.at(i)->getIsIon() == false && 
                                int(atomJ->getCharge()/Core::ELEMENTARY_CHARGE) != 0 &&
                                moleculesPtr.at(i)->getIsDipole() == false){
                        currentCharge = atomJ->getCharge();
                    }
                    
                    // if(distance.magnitude() > 3.e-10 && distance.magnitude() <= 22.e-10) {
                    //     eField[0] += distance.x() * currentCharge / distanceCubed; // E-field in x
                    //     eField[1] += distance.y() * currentCharge / distanceCubed; // E-field in y
                    //     eField[2] += distance.z() * currentCharge / distanceCubed; // E-field in z
                        
                    //     // derivative x to x
                    //     eFieldDerivative[0] += currentCharge / distanceCubed - 
                    //                             3 * currentCharge * distance.x() * distance.x() / (distanceCubed * distanceSquared); 
                    //     // derivative x to y
                    //     eFieldDerivative[1] += -3 * currentCharge * distance.x() * distance.y() / (distanceCubed * distanceSquared);
                    //     // derivative y to y
                    //     eFieldDerivative[2] += currentCharge / distanceCubed - 
                    //                             3 * currentCharge * distance.y() * distance.y() / (distanceCubed * distanceSquared);
                    //     // derivative y to z
                    //     eFieldDerivative[3] += -3 * currentCharge * distance.y() * distance.z() / (distanceCubed * distanceSquared);
                    //     // derivative z to z
                    //     eFieldDerivative[4] += currentCharge / distanceCubed - 
                    //                             3 * currentCharge * distance.z() * distance.z() / (distanceCubed * distanceSquared);
                    //     // derivative x to z
                    //     eFieldDerivative[5] += -3 * currentCharge * distance.x() * distance.z() / (distanceCubed * distanceSquared);
                    // }
                    if(distance.magnitude() <= 22e-10){
                        eField[0] += distance.x() * currentCharge / distanceCubed; // E-field in x
                        eField[1] += distance.y() * currentCharge / distanceCubed; // E-field in y
                        eField[2] += distance.z() * currentCharge / distanceCubed; // E-field in z
                        
                        // derivative x to x
                        eFieldDerivative[0] += currentCharge / distanceCubed - 
                                                3 * currentCharge * distance.x() * distance.x() / (distanceCubed * distanceSquared); 
                        // derivative x to y
                        eFieldDerivative[1] += -3 * currentCharge * distance.x() * distance.y() / (distanceCubed * distanceSquared);
                        // derivative y to y
                        eFieldDerivative[2] += currentCharge / distanceCubed - 
                                                3 * currentCharge * distance.y() * distance.y() / (distanceCubed * distanceSquared);
                        // derivative y to z
                        eFieldDerivative[3] += -3 * currentCharge * distance.y() * distance.z() / (distanceCubed * distanceSquared);
                        // derivative z to z
                        eFieldDerivative[4] += currentCharge / distanceCubed - 
                                                3 * currentCharge * distance.z() * distance.z() / (distanceCubed * distanceSquared);
                        // derivative x to z
                        eFieldDerivative[5] += -3 * currentCharge * distance.x() * distance.z() / (distanceCubed * distanceSquared);
                    }
                    

                    // Third contribution: ion <-> permanent dipole potential
                    // This requires an ion and a dipole to be present 
                    double dipoleDistanceScalar = 0;
                    double dipoleX = 0, dipoleY = 0, dipoleZ = 0;
                    currentCharge = 0;
                    if(int(atomI->getCharge()/Core::ELEMENTARY_CHARGE) != 0 && 
                        moleculesPtr.at(j)->getIsDipole() == true){

                        currentCharge = atomI->getCharge();
                        dipoleX = moleculesPtr.at(j)->getDipole().x();
                        dipoleY = moleculesPtr.at(j)->getDipole().y();
                        dipoleZ = moleculesPtr.at(j)->getDipole().z();
                        dipoleDistanceScalar =  dipoleX * distance.x() + 
                                                dipoleY * distance.y() + 
                                                dipoleZ * distance.z();

                    }else if (moleculesPtr.at(i)->getIsDipole() == true && 
                                int(atomJ->getCharge()/Core::ELEMENTARY_CHARGE) != 0){

                        currentCharge = atomJ->getCharge();
                        dipoleX = moleculesPtr.at(i)->getDipole().x();
                        dipoleY = moleculesPtr.at(i)->getDipole().y();
                        dipoleZ = moleculesPtr.at(i)->getDipole().z();
                        dipoleDistanceScalar =  dipoleX * distance.x() + 
                                                dipoleY * distance.y() + 
                                                dipoleZ * distance.z();
                    }
                    Core::Vector ionDipoleForce;
                    ionDipoleForce.x(-currentCharge * 1./Core::ELECTRIC_CONSTANT * 
                                        (1./distanceCubed * dipoleX - 
                                        3 * dipoleDistanceScalar * 1./(distanceCubed*distanceSquared) * distance.x()) );
                    ionDipoleForce.y(-currentCharge * 1./Core::ELECTRIC_CONSTANT * 
                                        (1./distanceCubed * dipoleY - 
                                        3 * dipoleDistanceScalar * 1./(distanceCubed*distanceSquared) * distance.y()) );
                    ionDipoleForce.z(-currentCharge * 1./Core::ELECTRIC_CONSTANT * 
                                        (1./distanceCubed * dipoleZ - 
                                        3 * dipoleDistanceScalar * 1./(distanceCubed*distanceSquared) * distance.z()) );
                    forceMolecules.at(i) += ionDipoleForce;
                    forceMolecules.at(j) += ionDipoleForce * (-1);
                    //std::cout << ionDipoleForce << std::endl;
                }
            }

            // add the C4 ion induced force
            Core::Vector ionInducedForce;
            ionInducedForce.x(1./Core::ELECTRIC_CONSTANT * collisionGasPolarizability_m3_ * 
                                (eField[0]*eFieldDerivative[0] + eField[1]*eFieldDerivative[1] + eField[2]*eFieldDerivative[5]));
            ionInducedForce.y(1./Core::ELECTRIC_CONSTANT * collisionGasPolarizability_m3_ * 
                                (eField[0]*eFieldDerivative[1] + eField[1]*eFieldDerivative[2] + eField[2]*eFieldDerivative[3]));
            ionInducedForce.z(1./Core::ELECTRIC_CONSTANT * collisionGasPolarizability_m3_ * 
                                (eField[0]*eFieldDerivative[5] + eField[1]*eFieldDerivative[3] + eField[2]*eFieldDerivative[4]));
            forceMolecules.at(i) += ionInducedForce;
            forceMolecules.at(j) += ionInducedForce * (-1);
            //std::cout << ionInducedForce << std::endl;

        }
    }
    // std::cout << forceMolecules.at(0) << std::endl;
    // std::cout << forceMolecules.at(1) << std::endl;
    return forceMolecules;
}
