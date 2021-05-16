/***************************
 Ion Dynamics Simulation Framework (IDSimF)

 Copyright 2020 - Physical and Theoretical Chemistry /
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

 ------------
 BT-staticSimionPASim.cpp

 Simple ion trajectory simulation of charged particles including
 pure particle / particle interaction (space charge) in a SIMION potential array

 ****************************/

#include "BTree_particle.hpp"
#include "BTree_tree.hpp"
#include "PSim_trajectoryExplorerJSONwriter.hpp"
#include "PSim_util.hpp"
#include "PSim_verletIntegrator.hpp"
#include "PSim_ionCloudReader.hpp"
#include "PSim_simionPotentialArray.hpp"
#include "CollisionModel_EmptyCollisionModel.hpp"
#include "appUtils_simulationConfiguration.hpp"
#include "appUtils_stopwatch.hpp"
#include <iostream>
#include <vector>
#include <ctime>


int main(int argc, const char * argv[]) {

    // read configuration file ======================================================================
    if (argc <2){
        std::cout << "no conf project name or conf file given"<<std::endl;
        return(1);
    }

    std::string confFileName = argv[1];
    AppUtils::SimulationConfiguration simConf(confFileName);

    std::string projectName = argv[2];
    std::cout << projectName<<std::endl;

    // read basic simulation parameters =============================================================
    int timeSteps = simConf.intParameter("sim_time_steps");
    int trajectoryWriteInterval = simConf.intParameter("trajectory_write_interval");
    double dt = simConf.doubleParameter("dt");
    std::string simionPAFilename = simConf.pathRelativeToConfFile(simConf.stringParameter("potential_array_file"));

    //read physical configuration ===================================================================
    double spaceChargeFactor = simConf.doubleParameter("space_charge_factor");


    //read simion potential array ===================================================================
    ParticleSimulation::SimionPotentialArray eField(simionPAFilename);

    //read ion configuration ========================================================================
    std::vector<std::unique_ptr<BTree::Particle>>particles;
    std::vector<BTree::Particle*>particlePtrs;

    std::string ionCloudFileName = simConf.pathRelativeToConfFile(simConf.stringParameter("ion_cloud_init_file"));
    ParticleSimulation::IonCloudReader reader = ParticleSimulation::IonCloudReader();
    particles = reader.readIonCloud(ionCloudFileName);
    //prepare a vector of raw pointers
    for (const auto& part : particles){
        particlePtrs.push_back(part.get());
    }

    //prepare file writer ==============================================================================
    auto jsonWriter = std::make_unique<ParticleSimulation::TrajectoryExplorerJSONwriter>(
            projectName + "_trajectories.json");
    jsonWriter->setScales(1000,1e6);


    // define functions for the trajectory integration ==================================================

    auto accelerationFunction =
            [spaceChargeFactor, &eField](
                    BTree::Particle *particle, int particleIndex,
                    BTree::Tree &tree, double time, int timestep) -> Core::Vector{

                Core::Vector pos = particle->getLocation();
                double particleCharge = particle->getCharge();
                try{
                    Core::Vector E = eField.getField(pos.x(),pos.y(),pos.z());
                    Core::Vector spaceChargeForce(0,0,0);
                    if (spaceChargeFactor > 0) {
                        spaceChargeForce =
                                tree.computeEFieldFromTree(*particle) * spaceChargeFactor;
                    }
                    return ((E  + spaceChargeForce) * particleCharge / particle->getMass());

                }
                catch (const std::invalid_argument& e){
                    particle->setActive(false);
                    return Core::Vector(0.0,0.0,0.0);
                }
            };

    // function to add some additional exported parameters to the exported trajectory file:
    ParticleSimulation::partAttribTransformFctType additionalParameterTransformFct =
            [](BTree::Particle *particle) -> std::vector<double>{
                std::vector<double> result = {
                        particle->getVelocity().x(),
                        particle->getVelocity().y(),
                        particle->getVelocity().z()
                };
                return result;
            };


    auto timestepWriteFunction =
            [trajectoryWriteInterval, &jsonWriter, &additionalParameterTransformFct](
                    std::vector<BTree::Particle *> &particles, BTree::Tree &tree, double time, int timestep,
                    bool lastTimestep){

                if (lastTimestep) {
                    jsonWriter->writeTimestep(
                            particles,
                            additionalParameterTransformFct,
                            time, true);

                    jsonWriter->writeSplatTimes(particles);
                    jsonWriter->writeIonMasses(particles);

                    std::cout << "finished ts:" << timestep << " time:" << time << std::endl;
                }

                else if (timestep % trajectoryWriteInterval == 0) {

                    std::cout << "ts:" << timestep << " time:" << time << std::endl;
                    jsonWriter->writeTimestep(
                            particles,
                            additionalParameterTransformFct,
                            time, false);
                }
    };


    // simulate ===============================================================================================
    AppUtils::Stopwatch stopWatch;
    stopWatch.start();
    ParticleSimulation::VerletIntegrator verletIntegrator(
            particlePtrs,
            accelerationFunction, timestepWriteFunction);
    verletIntegrator.run(timeSteps, dt);

    stopWatch.stop();

    std::cout << particles[0]->getLocation()<<std::endl;
    std::cout << "elapsed wall time:"<< stopWatch.elapsedSecondsWall()<<std::endl;
    return 0;
}