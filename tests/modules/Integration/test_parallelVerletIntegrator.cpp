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
 test_parallelVerletIntegrator.cpp

 Testing of parallelized verlet trajectory integrator

 ****************************/

#include "Integration_parallelVerletIntegrator.hpp"
#include "SC_generic.hpp"
#include "Core_vector.hpp"
#include "Core_particle.hpp"
#include "Core_randomGenerators.hpp"
#include "RS_Substance.hpp"
#include "RS_Simulation.hpp"
#include "catch.hpp"

using sMap = std::map<RS::Substance,int>;
using sPair= sMap::value_type;

TEST_CASE( "Test parallel verlet integrator", "[ParticleSimulation][ParallelVerletIntegrator][trajectory integration]") {
    //Set the global random generator to a test random number generator to make the test experiment fully deterministic:
    Core::globalRandomGeneratorPool = std::make_unique<Core::TestRandomGeneratorPool>();

    // init variables / functions
    double ionAcceleration = 10.0; //((1000V / 100mm) * elementary charge) / 100 amu = 9.64e9 m/s^2
    double dt = 1e-4;

    auto accelerationFct = [ionAcceleration](Core::Particle* /*particle*/, int /*particleIndex*/, SpaceCharge::FieldCalculator& /*tree*/,
                                             double /*time*/, int /*timestep*/){
        Core::Vector result(ionAcceleration, 0, ionAcceleration * 0.5);
        return (result);
    };

    Core::Particle testParticle1(Core::Vector(0.0, 0.0, 0.0),
            Core::Vector(0.0, 0.0, 0.0),
            1.0, 100.0);

    Core::Particle testParticle2(Core::Vector(0.0, 0.01, 0.0),
            Core::Vector(0.0, 0.0, 0.0),
            1.0, 100.0);


    SECTION( "Parallel Verlet integrator should be working with deferred particle addition") {

        // bare integrator without time step or other actions function
        Integration::ParallelVerletIntegrator verletIntegrator(accelerationFct);

        //should not crash / throw without particles
        REQUIRE_NOTHROW(verletIntegrator.run(1,dt));

        //particles should be addable and integrator should be able to run:
        unsigned int nSteps = 100;
        unsigned int nStepsLong = 1000;
        REQUIRE_NOTHROW(verletIntegrator.addParticle(&testParticle1));
        REQUIRE_NOTHROW(verletIntegrator.run(nSteps,dt));

        REQUIRE_NOTHROW(verletIntegrator.addParticle(&testParticle2));
        REQUIRE_NOTHROW(verletIntegrator.run(nSteps*2,dt));

        Core::Vector ionPos = testParticle2.getLocation();
        CHECK(Approx(ionPos.x()).epsilon(1e-6) == 0.00199);
        CHECK(Approx(ionPos.y()).epsilon(1e-2) == 0.01);
        CHECK(Approx(ionPos.z()).epsilon(1e-7) == 0.000995);

        REQUIRE_NOTHROW(verletIntegrator.run(nStepsLong,dt));

        ionPos = testParticle2.getLocation();
        CHECK(Approx(ionPos.x()).epsilon(1e-6) == 0.07194);
        CHECK(Approx(ionPos.y()).epsilon(1e-2) == 0.01);
        CHECK(Approx(ionPos.z()).epsilon(1e-7) == 0.03597);
    }

    SECTION("Tests with particle lists"){

        unsigned int nParticles = 10;
        unsigned int timeSteps = 60;

        std::vector<Core::uniquePartPtr>particles;
        std::vector<Core::Particle*>particlesPtrs;

        SECTION( "Parallel Verlet integrator should be able to integrate correctly non reactive particles with ToB") {

            //prepare particles with a distributed time of birth (TOB):
            double yPos = 0;
            double lastTime = timeSteps * dt - 4*dt;
            double timeOfBirth = lastTime;
            for (unsigned int i=0; i<nParticles; ++i){
                Core::uniquePartPtr particle = std::make_unique<Core::Particle>(
                        Core::Vector(0.0, yPos, 0.0),
                        Core::Vector(0.0,0.0,0.0),
                        1.0,
                        100.0,
                        timeOfBirth);
                particlesPtrs.push_back(particle.get());
                particles.push_back(std::move(particle));
                yPos += 0.01;
                timeOfBirth -= dt*0.5;
            }

            Integration::ParallelVerletIntegrator verletIntegrator(
                    particlesPtrs, accelerationFct, nullptr, nullptr, nullptr);

            verletIntegrator.run(timeSteps, dt);

            double endTime = timeSteps * dt;
            for (std::size_t i=0; i<nParticles; ++i){
                Core::Vector ionPos = particles[i]-> getLocation();

                //calculate approximate position according to a pure linear uniform acceleration
                // according to the real time the particles were present in the simulation:
                double diffTime = endTime - (0.5*dt)  - particles[i]-> getTimeOfBirth();
                double xCalculated= 0.5 * ionAcceleration * diffTime * diffTime;
                double zCalculated= 0.5 * xCalculated;

                CHECK(Approx(ionPos.x()).epsilon(0.05) == xCalculated);
                CHECK(Approx(ionPos.y()).epsilon(1e-7) == i*0.01);
                CHECK(Approx(ionPos.z()).epsilon(0.05) == zCalculated);
            }
        }

        SECTION( "Parallel Verlet integrator should be able to integrate correctly particles born at start") {

            //prepare particles all born at t=0:
            double yPos = 0;
            double timeOfBirth = 0.0;
            for (unsigned int i = 0; i<nParticles; ++i) {
                Core::uniquePartPtr particle = std::make_unique<Core::Particle>(
                        Core::Vector(0.0, yPos, 0.0),
                        Core::Vector(0.0, 0.0, 0.0),
                        1.0,
                        100.0,
                        timeOfBirth);
                particlesPtrs.push_back(particle.get());
                particles.push_back(std::move(particle));
                yPos += 0.01;
            }

            SECTION("Integration should run through and functions should be called") {

                unsigned int nTimestepsRecorded = 0;
                auto postTimestepFct = [&nTimestepsRecorded](
                        Integration::AbstractTimeIntegrator* /*integrator*/, std::vector<Core::Particle*>& /*particles*/,
                        double /*time*/, int /*timestep*/, bool /*lastTimestep*/){
                    nTimestepsRecorded++;
                };

                unsigned int nParticlesTouched = 0;
                auto otherActionsFct = [&nParticlesTouched] (
                        Core::Vector& /*newPartPos*/, Core::Particle* /*particle*/,
                        int /*particleIndex*/, double /*time*/, int /*timestep*/){
                    nParticlesTouched++;
                };

                unsigned int nParticlesStartMonitored = 0;
                auto particleStartMonitoringFct = [&nParticlesStartMonitored] (Core::Particle* /*particle*/, double /*time*/){
                    nParticlesStartMonitored++;
                };

                Integration::ParallelVerletIntegrator verletIntegrator(
                        particlesPtrs, accelerationFct, postTimestepFct, otherActionsFct, particleStartMonitoringFct);

                verletIntegrator.run(timeSteps, dt);

                CHECK(verletIntegrator.time() == Approx(timeSteps*dt));
                CHECK(verletIntegrator.timeStep() == timeSteps);

                double endTime = timeSteps*dt;
                for (std::size_t i = 0; i<nParticles; ++i) {
                    Core::Vector ionPos = particles[i]->getLocation();

                    //calculate approximate position according to a pure linear uniform acceleration
                    // according to the real time the particles were present in the simulation:
                    double diffTime = endTime-(0.5*dt)-particles[i]->getTimeOfBirth();
                    double xCalculated = 0.5*ionAcceleration*diffTime*diffTime;
                    double zCalculated = 0.5*xCalculated;

                    CHECK(Approx(ionPos.x()).epsilon(0.05)==xCalculated);
                    CHECK(Approx(ionPos.y()).epsilon(1e-7)==i*0.01);
                    CHECK(Approx(ionPos.z()).epsilon(0.05)==zCalculated);
                }

                CHECK(nTimestepsRecorded == timeSteps + 2);
                CHECK(nParticlesTouched == timeSteps * nParticles);
                CHECK(nParticlesStartMonitored == nParticles);
            }

            SECTION("Integration should be stoppable") {
                unsigned int terminationTimeStep = 40;

                unsigned int nTimestepsRecorded = 0;
                auto postTimestepFct = [&nTimestepsRecorded, terminationTimeStep](
                        Integration::AbstractTimeIntegrator* integrator, std::vector<Core::Particle*>& /*particles*/,
                        double /*time*/, unsigned int timestep, bool /*lastTimestep*/){
                    nTimestepsRecorded++;
                    if (timestep >= terminationTimeStep){
                        integrator->setTerminationState();
                    }
                };

                Integration::ParallelVerletIntegrator verletIntegrator(
                        particlesPtrs, accelerationFct, postTimestepFct);
                verletIntegrator.run(timeSteps, dt);
                CHECK(nTimestepsRecorded == terminationTimeStep+2);
                CHECK(verletIntegrator.timeStep() == terminationTimeStep);
                CHECK(verletIntegrator.time() == Approx(dt*(terminationTimeStep)));
            }
        }
    }
}
