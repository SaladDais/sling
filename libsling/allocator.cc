#include "allocator.hh"

thread_local ScriptAllocationManager *gAllocationManager = nullptr;

ScriptAllocationManager::ScriptAllocationManager() {}
ScriptAllocationManager::~ScriptAllocationManager() {
  for(auto &obj_ptr : _tracked_objects) {
    delete obj_ptr;
  }
  for(auto &obj_ptr : _mallocs) {
    free(obj_ptr);
  }
}