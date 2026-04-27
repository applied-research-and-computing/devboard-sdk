#pragma once

#include "carbon_instrument.h"

int carbon_registry_count(void);
const carbon_cmd_descriptor_t *carbon_registry_get(int index);
