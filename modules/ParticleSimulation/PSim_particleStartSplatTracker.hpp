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
 BTree_particleStartSplatTracker.hpp

 Tracker system to record particle start and stop (splat) times / positions

 ****************************/

#ifndef PSim_ionStartSplatTracker_hpp
#define PSim_ionStartSplatTracker_hpp

#include "Core_vector.hpp"
#include <unordered_map>
#include <vector>


//forward declare own classes:
namespace Core{
    class Particle;
}

namespace ParticleSimulation{



    /**
     * Tracker system to record particle start and stop (splat) times / positions. This separate tracking is
     * required, because simulations are allowed to destroy and generate particles at their will, for example
     * for simulations with a constant particle inflow. Thus an independent, global, tracking of start and
     * stop locations and times is required.
     */
    class ParticleStartSplatTracker {

    public:

        enum particleState {
            STARTED = 1,
            SPLATTED = 2,
            RESTARTED = 3,
            SPLATTED_AND_RESTARTED = 4
        };

        struct pMapEntry {
            std::size_t globalIndex;     ///< A particle index to identify the particle globally
            particleState state;         ///< The current state of the tracked particle
            double startTime=0;          ///< The start time of a particle
            double splatTime=0;          ///< The splat time of a particle
            Core::Vector startLocation;  ///< Start location of a particle
            Core::Vector splatLocation;  ///< Splat Location of a Particle
        };


        ParticleStartSplatTracker();

        void particleStart(Core::Particle* particle, double time);
        void particleRestart(Core::Particle* particle, Core::Vector oldPosition, Core::Vector newPosition, double time);
        void particleSplat(Core::Particle* particle, double time);

        [[nodiscard]] pMapEntry get(Core::Particle* particle) const;
        void sortStartSplatData();

        std::vector<pMapEntry> getStartSplatData() const;

        [[nodiscard]] std::vector<int> getSplatState() const;
        [[nodiscard]] std::vector<double> getStartTimes() const;
        [[nodiscard]] std::vector<double> getSplatTimes() const;
        [[nodiscard]] std::vector<Core::Vector> getStartLocations() const;
        [[nodiscard]] std::vector<Core::Vector> getSplatLocations() const;

    private:

        std::unordered_map<Core::Particle*, pMapEntry> pMap_;
        std::vector<pMapEntry> restartedParticlesData_;
        std::vector<pMapEntry> sortedParticleData_;
        std::size_t pInsertIndex_ = 0;
    };
}

#endif //PSim_ionStartSplatTracker_hpp
