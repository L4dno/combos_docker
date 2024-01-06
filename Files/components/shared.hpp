#pragma once
#include "types.hpp"

#define WARM_UP_TIME 20        // Warm up time in hours
#define MAX_SIMULATED_TIME 100 // Simulation time in hours

/* Simulation time */
const double maxtt = (MAX_SIMULATED_TIME + WARM_UP_TIME) * 3600; // Total simulation time in seconds
const double maxst = (MAX_SIMULATED_TIME) * 3600;                // Simulation time in seconds
const double maxwt = (WARM_UP_TIME) * 3600;                      // Warm up time in seconds

class SharedDatabase
{
public:
    /* Server info */
    static ProjectDatabase _pdatabase; // Projects databases
    static sserver_t _sserver_info;    // Scheduling servers information
    static dserver_t _dserver_info;    // Data servers information
    static dcserver_t _dcserver_info;  // Data client servers information
    static dclient_t _dclient_info;    // Data clients information
    static group_t _group_info;        // Client groups information
};

/*
 *	 Server compute simulation. Wait till the end of a executing task
 */
void compute_server(int flops);

/*
 *	Blank result
 */
AssignedResult *blank_result();

/*
 * to free memory, we delete all completed asynchronous communications. It shouldn't affect the clocks in the simmulator.
 */
void delete_completed_communications(std::vector<sg4::CommPtr> &pending_comms);
