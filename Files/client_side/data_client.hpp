#pragma once
#include <simgrid/s4u.hpp>

#include "components/types.hpp"
/*
 *	Data client requests function
 */
int data_client_requests(int argc, char *argv[]);

/*
 *	Data client dispatcher function
 */
int data_client_dispatcher(int argc, char *argv[]);

namespace for_testing
{
    void disk_access_test(int64_t size);
    int data_client_ask_for_files_test(ask_for_files_t params);
}