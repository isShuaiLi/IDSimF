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
 ****************************/

#include "Integration_fullSumVerletIntegrator.hpp"
#include <utility>
#include <algorithm>

Integration::FullSumVerletIntegrator::FullSumVerletIntegrator(
        const std::vector<Core::Particle *>& particles,
        Integration::accelerationFctSingleStepType accelerationFunction,
        Integration::postTimestepFctType timestepWriteFunction,
        Integration::otherActionsFctType otherActionsFunction,
        Integration::AbstractTimeIntegrator::particleStartMonitoringFctType ionStartMonitoringFunction,
        CollisionModel::AbstractCollisionModel* collisionModel) :
        AbstractTimeIntegrator(particles, ionStartMonitoringFunction),
        collisionModel_(collisionModel),
        accelerationFunction_(std::move(accelerationFunction)),
        postTimestepFunction_(std::move(timestepWriteFunction)),
        otherActionsFunction_(std::move(otherActionsFunction))
{}

Integration::FullSumVerletIntegrator::FullSumVerletIntegrator(
        Integration::accelerationFctSingleStepType accelerationFunction,
        Integration::postTimestepFctType timestepWriteFunction,
        Integration::otherActionsFctType otherActionsFunction,
        Integration::AbstractTimeIntegrator::particleStartMonitoringFctType ionStartMonitoringFunction,
        CollisionModel::AbstractCollisionModel* collisionModel) :
        AbstractTimeIntegrator(ionStartMonitoringFunction),
        collisionModel_(collisionModel),
        accelerationFunction_(std::move(accelerationFunction)),
        postTimestepFunction_(std::move(timestepWriteFunction)),
        otherActionsFunction_(std::move(otherActionsFunction))
{}


/**
 * Adds a particle to the verlet integrator (required if particles are generated in the course of the simulation
 * @param particle the particle to add to the verlet integration
 */
void Integration::FullSumVerletIntegrator::addParticle(Core::Particle *particle){
    particles_.push_back(particle);
    newPos_.emplace_back(Core::Vector(0,0,0));
    a_t_.emplace_back(Core::Vector(0,0,0));
    a_tdt_.emplace_back(Core::Vector(0,0,0));

    fullSumSolver_.insertParticle(*particle, nParticles_);
    ++nParticles_;
}

void Integration::FullSumVerletIntegrator::bearParticles_(double time) {
    Integration::AbstractTimeIntegrator::bearParticles_(time);
}


void Integration::FullSumVerletIntegrator::run(unsigned int nTimesteps, double dt) {

    // run init:
    this->runState_ = RUNNING;
    bearParticles_(0.0);

    if (postTimestepFunction_ !=nullptr) {
        postTimestepFunction_(this, particles_, time_, timestep_, false);
    }

    // run:
    for (unsigned int step=0; step< nTimesteps; step++){
        runSingleStep(dt);
        if (this->runState_ == IN_TERMINATION){
            break;
        }
    }
    this->finalizeSimulation();
    this->runState_ = STOPPED;
}

void Integration::FullSumVerletIntegrator::runSingleStep(double dt){
    bearParticles_(time_);
    if (collisionModel_ !=nullptr){
        collisionModel_->updateModelTimestepParameters(timestep_, time_);
    }
    std::size_t i;
    #pragma omp parallel \
            default(none) shared(newPos_, a_tdt_, a_t_, dt, particles_) \
            private(i) //firstprivate(MyNod)
    {

        #pragma omp for schedule(dynamic, 40)
        for (i=0; i<nParticles_; i++){

            if (particles_[i]->isActive()){

                if (collisionModel_ != nullptr) {
                    collisionModel_->updateModelParticleParameters(*(particles_[i]));
                }

                newPos_[i] = particles_[i]->getLocation() + particles_[i]->getVelocity() * dt + a_t_[i]*(1.0/2.0*dt*dt);
                a_tdt_[i] = accelerationFunction_(particles_[i], i, fullSumSolver_, time_, timestep_);
                //acceleration changes due to background interaction:

                if (collisionModel_ != nullptr) {
                    collisionModel_->modifyAcceleration(a_tdt_[i], *(particles_[i]), dt);
                }

                particles_[i]->setVelocity( particles_[i]->getVelocity() + ((a_t_[i]+ a_tdt_[i])*1.0/2.0 *dt) );
                a_t_[i] = a_tdt_[i];

                //velocity changes due to background interaction:
                if (collisionModel_ != nullptr) {
                    //std::cout << "before:" << particles_[i]->getVelocity() << std::endl;
                    collisionModel_->modifyVelocity(*(particles_[i]),dt);
                    //std::cout << "after:" << particles_[i]->getVelocity() << std::endl;
                }
            }
        }
    }

    // First find all new positions, then perform otherActions then update tree.
    // This ensures that all new particle positions are found with the state from
    // last time step. No particle positions are found with a partly updated tree.
    // Additionally, the update step is not (yet) parallel.
    for (std::size_t i=0; i<nParticles_; i++){
        if (particles_[i]->isActive()){
            //position changes due to background interaction:
            if (collisionModel_ != nullptr) {
                collisionModel_->modifyPosition(newPos_[i], *(particles_[i]), dt);
            }

            if (otherActionsFunction_ != nullptr) {
                otherActionsFunction_(newPos_[i], particles_[i], i, time_, timestep_);
            }
            particles_[i]->setLocation(newPos_[i]);
        }
    }
    time_ = time_ + dt;
    timestep_++;
    if (postTimestepFunction_ != nullptr) {
        postTimestepFunction_(this, particles_, time_, timestep_, false);
    }
}

/**
 * Finalizes the verlet integration run (should be called after the last time step).
 */
void Integration::FullSumVerletIntegrator::finalizeSimulation(){
    if (postTimestepFunction_ != nullptr){
        postTimestepFunction_(this, particles_, time_, timestep_, true);
    }
}


