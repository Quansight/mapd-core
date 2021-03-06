/*
 * Copyright 2017 MapD Technologies, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LOCKMGR_H
#define LOCKMGR_H

#include "Catalog/Catalog.h"
#include "Shared/mapd_shared_mutex.h"
#include "Shared/types.h"
#include "ThriftHandler/QueryState.h"

#include <map>
#include <mutex>
#include <tuple>
#include "boost/variant.hpp"

#include <rapidjson/document.h>

namespace Lock_Namespace {
using namespace rapidjson;
using LockTypeContainer = boost::variant<mapd_shared_lock<mapd_shared_mutex>,
                                         mapd_unique_lock<mapd_shared_mutex>>;

enum LockType { TableMetadataLock, CheckpointLock, ExecutorOuterLock, LockMax };

template <typename MutexType, typename KeyType>
class LockMgr {
 public:
  static std::shared_ptr<MutexType> getMutex(const LockType lockType, const KeyType& key);

 private:
  static std::mutex aMutex_;
  static std::map<std::tuple<LockType, KeyType>, std::shared_ptr<MutexType>> mutexMap_;
};

template <typename MutexType, typename KeyType>
std::mutex LockMgr<MutexType, KeyType>::aMutex_;
template <typename MutexType, typename KeyType>
std::map<std::tuple<LockType, KeyType>, std::shared_ptr<MutexType>>
    LockMgr<MutexType, KeyType>::mutexMap_;

template <typename MutexType, typename KeyType>
std::shared_ptr<MutexType> LockMgr<MutexType, KeyType>::getMutex(const LockType lock_type,
                                                                 const KeyType& key) {
  auto lock_key = std::make_tuple(lock_type, key);

  std::unique_lock<std::mutex> lck(aMutex_);
  auto mit = mutexMap_.find(lock_key);
  if (mit != mutexMap_.end()) {
    return mit->second;
  }

  auto tMutex = std::make_shared<MutexType>();
  mutexMap_[lock_key] = tMutex;
  return tMutex;
}

ChunkKey getTableChunkKey(const Catalog_Namespace::Catalog& cat,
                          const std::string& tableName);
void getTableNames(std::map<std::string, bool>& tableNames, const Value& value);
void getTableNames(std::map<std::string, bool>& tableNames, const std::string query_ra);
std::string parse_to_ra(query_state::QueryStateProxy, const std::string& query_str);

template <typename MutexType>
std::shared_ptr<MutexType> getTableMutex(const Catalog_Namespace::Catalog& cat,
                                         const std::string& tableName,
                                         const Lock_Namespace::LockType lockType) {
  return Lock_Namespace::LockMgr<MutexType, ChunkKey>::getMutex(
      lockType, getTableChunkKey(cat, tableName));
}

template <typename MutexType, template <typename> class LockType>
LockType<MutexType> getTableLock(const Catalog_Namespace::Catalog& cat,
                                 const std::string& tableName,
                                 const Lock_Namespace::LockType lockType) {
  auto lock = LockType<MutexType>(*getTableMutex<MutexType>(cat, tableName, lockType));
  // "... we need to make sure that the table (and after alter column) the columns are
  // still around after obtaining our locks ..."
  auto chunkKey = getTableChunkKey(cat, tableName);
  return lock;
}

template <typename MutexType>
void getTableLocks(const Catalog_Namespace::Catalog& cat,
                   const std::map<std::string, bool>& tableNames,
                   std::vector<std::shared_ptr<LockTypeContainer>>& tableLocks,
                   const Lock_Namespace::LockType lockType) {
  for (const auto& tableName : tableNames) {
    if (tableName.second) {
      tableLocks.emplace_back(std::make_shared<LockTypeContainer>(
          getTableLock<MutexType, mapd_unique_lock>(cat, tableName.first, lockType)));
    } else {
      tableLocks.emplace_back(std::make_shared<LockTypeContainer>(
          getTableLock<MutexType, mapd_shared_lock>(cat, tableName.first, lockType)));
    }
  }
}

template <typename MutexType>
void getTableLocks(const Catalog_Namespace::Catalog& cat,
                   const std::string& query_ra,
                   std::vector<std::shared_ptr<LockTypeContainer>>& tableLocks,
                   const Lock_Namespace::LockType lockType) {
  // parse ra to learn involved table names
  std::map<std::string, bool> tableNames;
  getTableNames(tableNames, query_ra);
  getTableLocks<MutexType>(cat, tableNames, tableLocks, lockType);
}

}  // namespace Lock_Namespace

#endif  // LOCKMGR_H
