/**
 *    Copyright (C) 2025-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/bson/bsonobj.h"
#include "mongo/db/matcher/expression_leaf.h"
#include "mongo/db/matcher/expression_tree.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/query/ce/ce_test_utils.h"
#include "mongo/db/query/ce/sampling/sampling_estimator_impl.h"
#include "mongo/db/query/ce/sampling/sampling_test_utils.h"
#include "mongo/db/query/multiple_collection_accessor.h"

#include <benchmark/benchmark.h>

#define MONGO_LOGV2_DEFAULT_COMPONENT ::mongo::logv2::LogComponent::kTest

namespace mongo::ce {

void initializeSamplingEstimator(SamplingEstimationBenchmarkConfiguration configuration,
                                 const size_t seedData,
                                 SamplingEstimatorTest& samplingEstimatorTest) {

    std::vector<stats::SBEValue> data;

    // Generate data according to the provided configuration
    generateData(configuration, seedData, data);

    samplingEstimatorTest.setUp();

    // Create vector of BSONObj according to the generated data
    // Number of fields dictates the number of columns the collection will have.
    auto dataBSON =
        SamplingEstimatorTest::createDocumentsFromSBEValue(data, configuration.numberOfFields);

    // Populate collection
    samplingEstimatorTest.insertDocuments(samplingEstimatorTest._kTestNss, dataBSON);
}

void BM_CreateSample(benchmark::State& state) {

    constexpr size_t seedData = 1724178214;

    SamplingEstimationBenchmarkConfiguration configuration(
        /*dataSize*/ state.range(0),
        /*dataDistribution*/ static_cast<DataDistributionEnum>(state.range(1)),
        /*dataType*/ static_cast<DataType>(state.range(2)),
        /*ndv*/ state.range(3),
        /*queryType*/ boost::none,
        /*numberOfFields*/ state.range(4),
        /*sampleSizeDef*/
        static_cast<SamplingEstimationBenchmarkConfiguration::SampleSizeDef>(state.range(5)),
        /*samplingAlgo-numOfChunks*/ state.range(6),
        /*numberOfQueries*/ boost::none);

    // Generate data and populate source collection
    SamplingEstimatorTest samplingEstimatorTest;
    initializeSamplingEstimator(configuration, seedData, samplingEstimatorTest);

    // Initialize collection accessor
    AutoGetCollection collPtr(samplingEstimatorTest.getOperationContext(),
                              samplingEstimatorTest._kTestNss,
                              LockMode::MODE_IX);
    MultipleCollectionAccessor collection =
        MultipleCollectionAccessor(samplingEstimatorTest.getOperationContext(),
                                   &collPtr.getCollection(),
                                   samplingEstimatorTest._kTestNss,
                                   /*isAnySecondaryNamespaceAViewOrNotFullyLocal*/ false,
                                   /*secondaryExecNssList*/ {});

    for (auto _ : state) {
        // Create sample from the provided collection
        SamplingEstimatorImpl samplingEstimator(
            samplingEstimatorTest.getOperationContext(),
            collection,
            configuration.sampleSize,
            configuration.samplingAlgo,
            configuration.numChunks,
            SamplingEstimatorTest::makeCardinalityEstimate(configuration.size));
    }
}

void BM_RunCardinalityEstimationOnSample(benchmark::State& state) {

    constexpr size_t seedData = 1724178214;
    constexpr size_t seedQueries = 2431475868;

    SamplingEstimationBenchmarkConfiguration configuration(
        /*dataSize*/ state.range(0),
        /*dataDistribution*/ static_cast<DataDistributionEnum>(state.range(1)),
        /*dataType*/ static_cast<DataType>(state.range(2)),
        /*ndv*/ state.range(3),
        /*queryType*/ static_cast<QueryType>(state.range(4)),
        /*numberOfFields*/ state.range(5),
        /*sampleSizeDef*/
        static_cast<SamplingEstimationBenchmarkConfiguration::SampleSizeDef>(state.range(6)),
        /*samplingAlgo-numOfChunks*/ state.range(7),
        /*numberOfQueries*/ state.range(8));

    // Generate data and populate source collection
    SamplingEstimatorTest samplingEstimatorTest;
    initializeSamplingEstimator(configuration, seedData, samplingEstimatorTest);

    // Initialize collection accessor
    AutoGetCollection collPtr(samplingEstimatorTest.getOperationContext(),
                              samplingEstimatorTest._kTestNss,
                              LockMode::MODE_IX);
    MultipleCollectionAccessor collection =
        MultipleCollectionAccessor(samplingEstimatorTest.getOperationContext(),
                                   &collPtr.getCollection(),
                                   samplingEstimatorTest._kTestNss,
                                   /*isAnySecondaryNamespaceAViewOrNotFullyLocal*/ false,
                                   /*secondaryExecNssList*/ {});

    // Create sample from the provided collection
    SamplingEstimatorImpl samplingEstimator(
        samplingEstimatorTest.getOperationContext(),
        collection,
        configuration.sampleSize,
        configuration.samplingAlgo,
        configuration.numChunks,
        SamplingEstimatorTest::makeCardinalityEstimate(configuration.size));

    // Generate queries.
    TypeProbability typeCombinationQuery{configuration.sbeDataType, 100, configuration.nanProb};
    if (configuration.dataType == kArray) {
        // The array data generation currently only supports integer elements as implemented in
        // populateTypeDistrVectorAccordingToInputConfig.
        typeCombinationQuery.typeTag = sbe::value::TypeTags::NumberInt64;
    }

    // Generate query intervals
    auto queryIntervals = generateIntervals(configuration.queryType.value(),
                                            configuration.dataInterval,
                                            configuration.numberOfQueries.value(),
                                            typeCombinationQuery,
                                            seedData,
                                            seedQueries);
    tassert(
        10472402, "queryIntervals should have at least one interval", queryIntervals.size() > 0);

    size_t i = 0;
    for (auto _ : state) {
        state.PauseTiming();
        auto first = queryIntervals[i].first;
        auto second = queryIntervals[i].second;
        auto matchExpr = createQueryMatchExpression(configuration.queryType.value(), first, second);
        state.ResumeTiming();
        benchmark::DoNotOptimize(samplingEstimator.estimateCardinality(matchExpr.get()));
        i = (i + 1) % queryIntervals.size();
    }
    state.SetItemsProcessed(state.iterations());
}

/**
 * Evaluate the performance of preparing the sampling CE estimator which mainly concentrates on
 * creating samples using a variety of Sampling strategies. This invocation will vary the number
 * documents and number of fields in the base collection as well as the sample size.
 */
BENCHMARK(BM_CreateSample)
    ->ArgNames({"dataSize",
                "dataDistribution",
                "dataType",
                "ndv",
                "numberOfFields",
                "sampleSizeDef",
                "samplingAlgo-numChunks"})
    ->ArgsProduct(
        {/*dataSize*/ {100},
         /*dataDistribution*/ {kUniform},
         /*dataType*/ {kInt},
         /*ndv*/ {10},
         /*numberOfFields*/ {1},
         /*sampleSizeDef*/
         {static_cast<int>(SamplingEstimationBenchmarkConfiguration::SampleSizeDef::ErrorSetting1)},
         /*samplingAlgo-numChunks*/ {/*random*/ -1}});

// Configuration of benchmark for evaluation:
// ->ArgsProduct(
//     {/*dataSize*/
//      {100000, 500000, 1000000},
//      /*dataDistribution*/ {kUniform},
//      /*dataType*/ {kInt, kString},
//      /*ndv*/ {1000},
//      /*numberOfFields*/ {1, 20},
//      /*sampleSizeDef*/
//      {static_cast<int>(SamplingEstimationBenchmarkConfiguration::SampleSizeDef::ErrorSetting1)},
//      /*samplingAlgo-numChunks*/ {/*random*/ -1, /*chunk*/ 10}});

/**
 * Evaluate the performance of estimating CE using an already populated sample. The
 * estimation mainly concentrates on processing the already existing sample and
 * extrapolating the cardinality results. This invocation will vary the number documents and
 * number of fields in the base collection, the type of queries (point and range), as well
 * as the sample size.
 */
BENCHMARK(BM_RunCardinalityEstimationOnSample)
    ->ArgNames({"dataSize",
                "dataDistribution",
                "dataType",
                "ndv",
                "queryType",
                "numberOfFields",
                "sampleSizeDef",
                "samplingAlgo-numChunks",
                "numberOfQueries"})
    ->ArgsProduct(
        {/*dataSize*/
         {100},
         /*dataDistribution*/ {kUniform},
         /*dataType*/ {kInt},
         /*ndv*/ {10},
         /*queryType*/ {kPoint},
         /*numberOfFields*/ {1},
         /*sampleSizeDef*/
         {static_cast<int>(SamplingEstimationBenchmarkConfiguration::SampleSizeDef::ErrorSetting1)},
         /*samplingAlgo-numChunks*/ {/*random*/ -1},
         /*numberOfQueries*/ {1}});

// Configuration of benchmark for evaluation:
// ->ArgsProduct(
//     {/*dataSize*/
//      {100000},
//      /*dataDistribution*/ {kUniform},
//      /*dataType*/ {kInt, kString},
//      /*ndv*/ {1000},
//      /*queryType*/ {kPoint, kRange},
//      /*numberOfFields*/ {1},
//      /*sampleSizeDef*/
//      {static_cast<int>(SamplingEstimationBenchmarkConfiguration::SampleSizeDef::ErrorSetting1)},
//      /*samplingAlgo-numChunks*/ {/*random*/ -1},
//      /*numberOfQueries*/ {1}});

}  // namespace mongo::ce
