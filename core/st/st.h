#pragma once

// Umbrella header for the L-series batch L0 ST language layer (approved
// st-l0-semantics): compile() in the load domain, Instance in the cycle
// domain. See core/st/README.md for the layering contract.

#include "st/binding.h"
#include "st/binding_manifest.h"
#include "st/binding_registry.h"
#include "st/compile.h"
#include "st/process_image.h"
#include "st/standard_functions.h"
#include "st/tasking.h"
#include "st/vm.h"
#include "st/configuration_runtime.h"
#include "st/debug.h"
