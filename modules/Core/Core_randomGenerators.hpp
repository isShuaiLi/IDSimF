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

 Core_randomGenerators.hpp

 Random number generators for the Core / IDSimF Project

 Simple abstraction classes for random number generation,
 for simulation and testing use

 ****************************/

#ifndef BTree_randomGenerators
#define BTree_randomGenerators

#include "Core_randomTestSamples.hpp"
#include <random>
#include <vector>
#include <memory>

namespace Core{

    using rndBit_type = std::mt19937::result_type; ///< Result type of random bit generators

    namespace XoshiroPRNG{
        using rndBit_type = uint_fast64_t; ///< Result type of random bit generator for xoshiro256+ PRNG
        inline constexpr rndBit_type defaultSeed = 1234567890ULL; ///< Default seed
    }
    

    extern std::random_device rdSeed; ///< global seed generator

    /**
     * Generalized source for random bits, which can be used as random bit source for random distributions
     *
     * Note: Since min and max has to be constexpr, min and max are fixed for all implmementations of this
     * interface class.
     */
    template <class result_T>
    class RandomBitSource{
    public:
        typedef result_T result_type;
        virtual ~RandomBitSource() =default;

        /**
         * Note: Min is fixed to mersenne twister min
         */
        constexpr static rndBit_type min(){
            return std::mt19937::min();
        };

        /**
         * Note: Max is fixed to mersenne twister max
         */
        constexpr static rndBit_type max(){
            return std::mt19937::max();
        };
        virtual result_T operator()() =0;
    };

    /**
     * SplitMix64 algorithm, which can be used as random bit source for random distributions
     * Reference: https://prng.di.unimi.it/splitmix64.c
     *
     * Note: Since min and max has to be constexpr, min and max are fixed for all implmementations of this
     * interface class.
     */
    template <class result_T>
    class SplitMix64{
    public:
        typedef result_T result_type;
        virtual ~SplitMix64() =default;

        /**
         * Note: Min is fixed to xoshiro datatype min
         */
        constexpr static XoshiroPRNG::rndBit_type min(){
            return XoshiroPRNG::rndBit_type(0);
        };

        /**
         * Note: Max is fixed to xoshiro datatype max
         */
        constexpr static XoshiroPRNG::rndBit_type max(){
            return XoshiroPRNG::rndBit_type(-1);
        };
        virtual result_T operator()() =0;
    };

    /**
     * Random bit source based on mersenne twister
     */
    class MersenneBitSource: public RandomBitSource<rndBit_type>{
    public:
        MersenneBitSource();
        void seed(rndBit_type seed);
        rndBit_type operator()() override;

        std::mt19937 internalRandomSource;
    };

    /**
     * Random bit source, which generates NO random bits but a short sequence of predefined
     * bits for testing purposes
     */
    class TestBitSource: public RandomBitSource<rndBit_type>{
    public:
        TestBitSource();
        rndBit_type operator()() override;

    private:
        std::size_t sampleIndex_;
    };

    /**
     * Random test bit source based on SplitMix64, generating a large set of deterministic numbers based on 
     * predefined seed
     */
    class SplitMix64TestBitSource: public SplitMix64<XoshiroPRNG::rndBit_type>{
    public:
        SplitMix64TestBitSource();
        XoshiroPRNG::rndBit_type operator()() override;

    private:
        XoshiroPRNG::rndBit_type state_;
    };

    /**
     * Generalized source for randomness. Allows to produce uniformly and normal distributed random values
     * and random bits
     */
    class RandomSource{
    public:
        virtual ~RandomSource() =default;
        virtual double uniformRealRndValue() =0;
        virtual double normalRealRndValue() =0;
        virtual RandomBitSource<rndBit_type>* getRandomBitSource() =0;
    };

    /**
     * Random distribution which produces random samples with a specific distribution
     */
    class RandomDistribution{
    public:
        virtual double rndValue() =0;
        virtual ~RandomDistribution() = default;
    };

    using RndDistPtr= std::unique_ptr<Core::RandomDistribution>; ///< pointer type used throughout the project

    /**
     * Uniform random distribution, which generates random samples in a specified interval
     */
    class UniformRandomDistribution: public RandomDistribution{
    public:
        UniformRandomDistribution(double min, double max, RandomBitSource<rndBit_type>* randomSource);
        double rndValue() override;
    private:
        RandomBitSource<rndBit_type>* randomSource_;
        std::uniform_real_distribution<double> internalUniformDist_;
    };

    /**
     * A uniform distribution, which generates *non* random test samples in a specified interval.
     * The samples are sequentially chosen from a small set of predetermined test values
     */
    class UniformTestDistribution: public RandomDistribution{
    public:
        UniformTestDistribution() = default;
        UniformTestDistribution(double min, double max);
        double rndValue() override;
    private:
        std::size_t sampleIndex_ = 0;
        double min_ = 0.0;
        double interval_ = 1.0;
    };

    /**
     * Test random distribution which *non* random samples which are gaussian normal distributed (with mu=0.0 and sigma=1.0).
     * The samples are sequentially chosen from a small set of predetermined test values
     */
    class NormalTestDistribution: public RandomDistribution{
    public:
        NormalTestDistribution();
        double rndValue() override;
    private:
        std::size_t sampleIndex_;
    };

    /**
     * Generalized pool of random sources, usually one per thread. This allows to provide individual, seperate
     * random sources for individual threads and can produce random distribution objects.
     */
    class AbstractRandomGeneratorPool{
    public:
        virtual ~AbstractRandomGeneratorPool() =default;
        virtual void setSeedForElements(rndBit_type newSeed) =0;
        virtual RndDistPtr getUniformDistribution(double min, double max) =0;
        virtual RandomSource* getThreadRandomSource() =0;
        virtual RandomSource* getRandomSource(std::size_t index) =0;
    };

    /**
     * Pool of random sources, with random sources based on Mersenne Twister
     */
    class RandomGeneratorPool: public AbstractRandomGeneratorPool{
    public:
        class RNGPoolElement: public RandomSource{
        public:
            RNGPoolElement() = default;
            void seed(rndBit_type seed);
            double uniformRealRndValue() override;
            double normalRealRndValue() override;
            MersenneBitSource* getRandomBitSource() override;

        private:
            MersenneBitSource rngGenerator_;
            std::uniform_real_distribution<double> uniformDist_;
            std::normal_distribution<double> normalDist_;
        };

        RandomGeneratorPool();
        void setSeedForElements(rndBit_type newSeed) override;
        RndDistPtr getUniformDistribution(double min, double max) override;
        RNGPoolElement* getThreadRandomSource() override;
        RNGPoolElement* getRandomSource(std::size_t index) override;

    private:
        std::vector<std::unique_ptr<RNGPoolElement>> elements_;
    };

    /**
     * Pool of "random" sources, with test random sources and test distributions which produce short, predifined
     * sequences of values / bits.
     */
    class TestRandomGeneratorPool: public AbstractRandomGeneratorPool{
    public:
        class TestRNGPoolElement: public RandomSource{
        public:
            TestRNGPoolElement() = default;
            double uniformRealRndValue() override;
            double normalRealRndValue() override;
            TestBitSource* getRandomBitSource() override;

        private:
            TestBitSource rngGenerator_;
            UniformTestDistribution uniformDist_;
            NormalTestDistribution normalDist_;
        };

        TestRandomGeneratorPool() = default;
        void setSeedForElements(rndBit_type newSeed) override;
        RndDistPtr getUniformDistribution(double min, double max) override;
        TestRNGPoolElement* getThreadRandomSource() override;
        TestRNGPoolElement* getRandomSource(std::size_t index) override;

    private:
        TestRNGPoolElement element_;

    };

    extern std::unique_ptr<AbstractRandomGeneratorPool> globalRandomGeneratorPool; ///< global random pool / randomness provider
}

#endif //BTree_randomGenerators
