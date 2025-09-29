/**
 *    Copyright (C) 2022-present MongoDB, Inc.
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

#pragma once

#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/db/s/metrics/cumulative_metrics_state_tracker.h"
#include "mongo/db/s/metrics/field_names/sharding_data_transform_cumulative_metrics_field_name_provider.h"
#include "mongo/db/s/metrics/sharding_data_transform_metrics.h"
#include "mongo/db/s/metrics/sharding_data_transform_metrics_observer_interface.h"
#include "mongo/db/service_context.h"
#include "mongo/platform/atomic_word.h"
#include "mongo/s/resharding/common_types_gen.h"
#include "mongo/stdx/mutex.h"
#include "mongo/util/concurrency/with_lock.h"
#include "mongo/util/duration.h"
#include "mongo/util/functional.h"
#include "mongo/util/time_support.h"
#include "mongo/util/uuid.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <boost/optional/optional.hpp>

namespace mongo {

class ShardingDataTransformCumulativeMetrics {
public:
    using NameProvider = ShardingDataTransformCumulativeMetricsFieldNameProvider;
    using Role = ShardingDataTransformMetrics::Role;
    using InstanceObserver = ShardingDataTransformMetricsObserverInterface;
    using DeregistrationFunction = unique_function<void()>;
    using StateTracker =
        CumulativeMetricsStateTracker<CoordinatorStateEnum, DonorStateEnum, RecipientStateEnum>;
    using AnyState = StateTracker::AnyState;

    struct MetricsComparer {
        inline bool operator()(const InstanceObserver* a, const InstanceObserver* b) const {
            auto aTime = a->getStartTimestamp();
            auto bTime = b->getStartTimestamp();
            if (aTime == bTime) {
                return a->getUuid() < b->getUuid();
            }
            return aTime < bTime;
        }
    };
    using MetricsSet = std::set<const InstanceObserver*, MetricsComparer>;

    /**
     * RAII type that takes care of deregistering the observer once it goes out of scope.
     */
    class ScopedObserver {
    public:
        ScopedObserver(ShardingDataTransformCumulativeMetrics* metrics,
                       Role role,
                       MetricsSet::iterator observerIterator);
        ScopedObserver(const ScopedObserver&) = delete;
        ScopedObserver& operator=(const ScopedObserver&) = delete;

        ~ScopedObserver();

    private:
        ShardingDataTransformCumulativeMetrics* const _metrics;
        const Role _role;
        const MetricsSet::iterator _observerIterator;
    };

    using UniqueScopedObserver = std::unique_ptr<ScopedObserver>;
    friend ScopedObserver;

    static ShardingDataTransformCumulativeMetrics* getForResharding(ServiceContext* context);
    static ShardingDataTransformCumulativeMetrics* getForMoveCollection(ServiceContext* context);
    static ShardingDataTransformCumulativeMetrics* getForBalancerMoveCollection(
        ServiceContext* context);
    static ShardingDataTransformCumulativeMetrics* getForUnshardCollection(ServiceContext* context);
    static ShardingDataTransformCumulativeMetrics* getForMovePrimary(ServiceContext* context);

    ShardingDataTransformCumulativeMetrics(const std::string& rootSectionName,
                                           std::unique_ptr<NameProvider> fieldNameProvider);
    virtual ~ShardingDataTransformCumulativeMetrics() = default;
    [[nodiscard]] UniqueScopedObserver registerInstanceMetrics(const InstanceObserver* metrics);
    int64_t getOldestOperationHighEstimateRemainingTimeMillis(Role role) const;
    int64_t getOldestOperationLowEstimateRemainingTimeMillis(Role role) const;
    size_t getObservedMetricsCount() const;
    size_t getObservedMetricsCount(Role role) const;
    virtual void reportForServerStatus(BSONObjBuilder* bob) const;

    void onStarted();
    void onSuccess();
    void onFailure();
    void onCanceled();

    void setLastOpEndingChunkImbalance(int64_t imbalanceCount);

    void onReadDuringCriticalSection();
    void onWriteDuringCriticalSection();
    void onWriteToStashedCollections();

    void onCloningRemoteBatchRetrieval(Milliseconds elapsed);
    void onInsertsDuringCloning(int64_t count, int64_t bytes, const Milliseconds& elapsedTime);

    void onInsertApplied();
    void onUpdateApplied();
    void onDeleteApplied();
    void onOplogEntriesFetched(int64_t numEntries);
    void onOplogEntriesApplied(int64_t numEntries);

    void onBatchRetrievedDuringOplogFetching(Milliseconds elapsed);
    void onLocalInsertDuringOplogFetching(const Milliseconds& elapsedTime);
    void onBatchRetrievedDuringOplogApplying(const Milliseconds& elapsedTime);
    void onOplogLocalBatchApplied(Milliseconds elapsed);

    template <typename T>
    void onStateTransition(boost::optional<T> before, boost::optional<T> after) {
        _stateTracker.onStateTransition(before, after);
    }

protected:
    const ShardingDataTransformCumulativeMetricsFieldNameProvider* getFieldNames() const;

    virtual void reportActive(BSONObjBuilder* bob) const;
    virtual void reportOldestActive(BSONObjBuilder* bob) const;
    virtual void reportLatencies(BSONObjBuilder* bob) const;
    virtual void reportCurrentInSteps(BSONObjBuilder* bob) const;

    int64_t getInsertsApplied() const;
    int64_t getUpdatesApplied() const;
    int64_t getDeletesApplied() const;
    int64_t getOplogEntriesFetched() const;
    int64_t getOplogEntriesApplied() const;

    int64_t getOplogFetchingTotalRemoteBatchesRetrieved() const;
    int64_t getOplogFetchingTotalRemoteBatchesRetrievalTimeMillis() const;
    int64_t getOplogFetchingTotalLocalInserts() const;
    int64_t getOplogFetchingTotalLocalInsertTimeMillis() const;
    int64_t getOplogApplyingTotalBatchesRetrieved() const;
    int64_t getOplogApplyingTotalBatchesRetrievalTimeMillis() const;
    int64_t getOplogBatchApplied() const;
    int64_t getOplogBatchAppliedMillis() const;

    void reportCountsForAllStates(const StateTracker::StateFieldNameMap& names,
                                  BSONObjBuilder* bob) const;

    template <typename FieldNameProvider>
    void reportOplogApplicationCountMetrics(const FieldNameProvider* names,
                                            BSONObjBuilder* bob) const {

        bob->append(names->getForOplogEntriesFetched(), getOplogEntriesFetched());
        bob->append(names->getForOplogEntriesApplied(), getOplogEntriesApplied());
        bob->append(names->getForInsertsApplied(), getInsertsApplied());
        bob->append(names->getForUpdatesApplied(), getUpdatesApplied());
        bob->append(names->getForDeletesApplied(), getDeletesApplied());
    }

    template <typename FieldNameProvider>
    void reportOplogApplicationLatencyMetrics(const FieldNameProvider* names,
                                              BSONObjBuilder* bob) const {
        bob->append(names->getForOplogFetchingTotalRemoteBatchRetrievalTimeMillis(),
                    getOplogFetchingTotalRemoteBatchesRetrievalTimeMillis());
        bob->append(names->getForOplogFetchingTotalRemoteBatchesRetrieved(),
                    getOplogFetchingTotalRemoteBatchesRetrieved());
        bob->append(names->getForOplogFetchingTotalLocalInsertTimeMillis(),
                    getOplogFetchingTotalLocalInsertTimeMillis());
        bob->append(names->getForOplogFetchingTotalLocalInserts(),
                    getOplogFetchingTotalLocalInserts());
        bob->append(names->getForOplogApplyingTotalLocalBatchRetrievalTimeMillis(),
                    getOplogApplyingTotalBatchesRetrievalTimeMillis());
        bob->append(names->getForOplogApplyingTotalLocalBatchesRetrieved(),
                    getOplogApplyingTotalBatchesRetrieved());
        bob->append(names->getForOplogApplyingTotalLocalBatchApplyTimeMillis(),
                    getOplogBatchAppliedMillis());
        bob->append(names->getForOplogApplyingTotalLocalBatchesApplied(), getOplogBatchApplied());
    }

    template <typename T>
    int64_t getCountInState(T state) const {
        return _stateTracker.getCountInState(state);
    }

    const std::string _rootSectionName;
    AtomicWord<bool> _operationWasAttempted;

private:
    enum EstimateType { kHigh, kLow };

    MetricsSet& getMetricsSetForRole(Role role);
    const MetricsSet& getMetricsSetForRole(Role role) const;
    const InstanceObserver* getOldestOperation(WithLock, Role role) const;
    int64_t getOldestOperationEstimateRemainingTimeMillis(Role role, EstimateType type) const;
    boost::optional<Milliseconds> getEstimate(const InstanceObserver* op, EstimateType type) const;

    MetricsSet::iterator insertMetrics(const InstanceObserver* metrics, MetricsSet& set);
    void deregisterMetrics(const Role& role, const MetricsSet::iterator& metrics);

    mutable stdx::mutex _mutex;
    std::unique_ptr<NameProvider> _fieldNames;
    std::vector<MetricsSet> _instanceMetricsForAllRoles;

    StateTracker _stateTracker;

    AtomicWord<int64_t> _countStarted{0};
    AtomicWord<int64_t> _countSucceeded{0};
    AtomicWord<int64_t> _countFailed{0};
    AtomicWord<int64_t> _countCancelled{0};

    AtomicWord<int64_t> _totalBatchRetrievedDuringClone{0};
    AtomicWord<int64_t> _totalBatchRetrievedDuringCloneMillis{0};
    AtomicWord<int64_t> _documentsProcessed{0};
    AtomicWord<int64_t> _bytesWritten{0};

    AtomicWord<int64_t> _lastOpEndingChunkImbalance{0};
    AtomicWord<int64_t> _readsDuringCriticalSection{0};
    AtomicWord<int64_t> _writesDuringCriticalSection{0};

    AtomicWord<int64_t> _collectionCloningTotalLocalBatchInserts{0};
    AtomicWord<int64_t> _collectionCloningTotalLocalInsertTimeMillis{0};
    AtomicWord<int64_t> _writesToStashedCollections{0};

    AtomicWord<int64_t> _insertsApplied{0};
    AtomicWord<int64_t> _updatesApplied{0};
    AtomicWord<int64_t> _deletesApplied{0};
    AtomicWord<int64_t> _oplogEntriesApplied{0};
    AtomicWord<int64_t> _oplogEntriesFetched{0};

    AtomicWord<int64_t> _oplogFetchingTotalRemoteBatchesRetrieved{0};
    AtomicWord<int64_t> _oplogFetchingTotalRemoteBatchesRetrievalTimeMillis{0};
    AtomicWord<int64_t> _oplogFetchingTotalLocalInserts{0};
    AtomicWord<int64_t> _oplogFetchingTotalLocalInsertTimeMillis{0};
    AtomicWord<int64_t> _oplogApplyingTotalBatchesRetrieved{0};
    AtomicWord<int64_t> _oplogApplyingTotalBatchesRetrievalTimeMillis{0};
    AtomicWord<int64_t> _oplogBatchApplied{0};
    AtomicWord<int64_t> _oplogBatchAppliedMillis{0};
};

}  // namespace mongo
