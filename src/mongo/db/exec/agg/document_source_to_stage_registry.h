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

#pragma once

#include <boost/intrusive_ptr.hpp>

#include "mongo/base/init.h"
#include "mongo/db/exec/agg/stage.h"
#include "mongo/db/pipeline/document_source.h"

namespace mongo {
namespace exec {
namespace agg {

/**
 * Register a function that builds an aggregation 'Stage' from a 'DocumentSource'.
 *
 * 'name' is a unique name to give to the initializer function that does the
 * registration.
 *
 * 'documentSourceId' is a unique DocumentSource::Id that is assigned to the
 * DocumentSource class.
 *
 * 'documentSourceToStageFn' is a function that accepts pointer to the DocumentSource
 * and returns pointer to the Stage.
 */
#define REGISTER_AGG_STAGE_MAPPING(name, documentSourceId, documentSourceToStageFn) \
    namespace {                                                                     \
    MONGO_INITIALIZER_GENERAL(registerAggStageMapping_##name,                       \
                              ("BeginDocumentSourceStageRegistration"),             \
                              ("EndDocumentSourceStageRegistration"))               \
    (InitializerContext*) {                                                         \
        registerDocumentSourceToStageFn(documentSourceId, documentSourceToStageFn); \
    }                                                                               \
    }

using DocumentSourceToStageFn =
    std::function<boost::intrusive_ptr<Stage>(const boost::intrusive_ptr<const DocumentSource>&)>;

/**
 * Registers a DocumentSource with a function that builds an aggregation 'Stage' from
 * a 'DocumentSource'.
 *
 * DO NOT call this funciton directly. Instead, use the REGISTER_AGG_STAGE_MAPPING
 * macro defined in this file.
 */
void registerDocumentSourceToStageFn(DocumentSource::Id dsid, DocumentSourceToStageFn fn);

/**
 * For an instance of DocumentSource create appropriate Stage object.
 */
boost::intrusive_ptr<Stage> buildStage(const boost::intrusive_ptr<DocumentSource>& ds);

}  // namespace agg
}  // namespace exec
}  // namespace mongo
