#pragma once

#include <simgrid/s4u.hpp>

#include "components/types.hpp"

int client_execute_tasks(ProjectInstanceOnClient *proj);

std::pair<int, int> client_main_loop(client_t client);