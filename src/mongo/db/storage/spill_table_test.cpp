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

#include "mongo/db/storage/spill_table.h"

#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/record_id.h"
#include "mongo/db/service_context.h"
#include "mongo/db/storage/disk_space_monitor.h"
#include "mongo/db/storage/storage_engine_test_fixture.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/fail_point.h"

#include <memory>
#include <vector>

namespace mongo {
namespace {

class SpillTableTest : public StorageEngineTest {
protected:
    SpillTableTest()
        : StorageEngineTest(
              StorageEngineTest::Options{}.setParameter("featureFlagCreateSpillKVEngine", true)) {}
};

TEST_F(SpillTableTest, ImmediatelyBelowDiskSpaceThreshold) {
    auto thresholdBytes = 1024;
    FailPointEnableBlock fp{"simulateAvailableDiskSpace", BSON("bytes" << thresholdBytes - 1)};
    auto opCtx = makeOperationContext();
    auto spillTable = makeSpillTable(opCtx.get(), KeyFormat::Long, thresholdBytes);

    auto obj = BSON("a" << 1);
    std::vector<Record> records{{RecordId(), {obj.objdata(), obj.objsize()}}};

    ASSERT_EQ(spillTable->insertRecords(opCtx.get(), &records), ErrorCodes::OutOfDiskSpace);
    ASSERT_EQ(spillTable->updateRecord(opCtx.get(), RecordId{1}, obj.objdata(), obj.objsize()),
              ErrorCodes::OutOfDiskSpace);
    ASSERT_THROWS_CODE(spillTable->deleteRecord(opCtx.get(), RecordId{1}),
                       DBException,
                       ErrorCodes::OutOfDiskSpace);
    ASSERT_EQ(spillTable->truncate(opCtx.get()), ErrorCodes::OutOfDiskSpace);
    ASSERT_EQ(spillTable->rangeTruncate(opCtx.get(), RecordId::minLong(), RecordId::maxLong()),
              ErrorCodes::OutOfDiskSpace);
}

TEST_F(SpillTableTest, LaterBelowDiskSpaceThreshold) {
    auto thresholdBytes = 1024;
    auto opCtx = makeOperationContext();
    auto spillTable = makeSpillTable(opCtx.get(), KeyFormat::Long, thresholdBytes);

    auto obj = BSON("a" << 1);
    std::vector<Record> records{{RecordId(), {obj.objdata(), obj.objsize()}}};
    ASSERT_OK(spillTable->insertRecords(opCtx.get(), &records));
    auto rid = records[0].id;
    records[0].id = {};

    ASSERT_OK(spillTable->insertRecords(opCtx.get(), &records));
    ASSERT_OK(spillTable->updateRecord(opCtx.get(), rid, obj.objdata(), obj.objsize()));
    ASSERT_DOES_NOT_THROW(spillTable->deleteRecord(opCtx.get(), records[0].id));
    ASSERT_OK(spillTable->truncate(opCtx.get()));
    ASSERT_OK(spillTable->rangeTruncate(opCtx.get(), rid, rid));

    FailPointEnableBlock fp{"simulateAvailableDiskSpace", BSON("bytes" << thresholdBytes - 1)};
    DiskSpaceMonitor::get(opCtx->getServiceContext())->runAllActions(opCtx.get());

    ASSERT_EQ(spillTable->insertRecords(opCtx.get(), &records), ErrorCodes::OutOfDiskSpace);
    ASSERT_EQ(spillTable->updateRecord(opCtx.get(), rid, obj.objdata(), obj.objsize()),
              ErrorCodes::OutOfDiskSpace);
    ASSERT_THROWS_CODE(
        spillTable->deleteRecord(opCtx.get(), rid), DBException, ErrorCodes::OutOfDiskSpace);
    ASSERT_EQ(spillTable->truncate(opCtx.get()), ErrorCodes::OutOfDiskSpace);
    ASSERT_EQ(spillTable->rangeTruncate(opCtx.get(), rid, rid), ErrorCodes::OutOfDiskSpace);
}

}  // namespace
}  // namespace mongo
