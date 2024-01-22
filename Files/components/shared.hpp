#pragma once
#include "types.hpp"

// todo: fix ./generator to set values in this file, not in boinc.cpp
extern int WARM_UP_TIME;       // Warm up time in hours
extern int MAX_SIMULATED_TIME; // Simulation time in hours
#define PRECISION 0.00001      // Accuracy (used in client_work_fetch())
#define REPLY_SIZE 10 * KB     // Reply size
#define CREDITS_CPU_S 0.002315 // Credits per second (1 GFLOP machine)
#define WORK_FETCH_PERIOD 60   // Work fetch period
#define REQUEST_SIZE 10 * KB   // Request size

// #define NUMBER_CLIENTS 1000     // Number of clients
// #define NUMBER_DATA_CLIENTS 100 // Number of data clients
// #define NUMBER_ORDINARY_CLIENTS (NUMBER_CLIENTS - NUMBER_DATA_CLIENTS)
extern int g_total_number_clients;      // Number of clients
extern int g_total_number_data_clients; // Number of data clients
extern int g_total_number_ordinary_clients;

/* Simulation time */
extern double maxtt; // Total simulation time in seconds
extern double maxst; // Simulation time in seconds
extern double maxwt; // Warm up time in seconds

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
 * to free memory, we delete all completed asynchronous communications. It shouldn't affect the clocks in the simmulator.
 */
void delete_completed_communications(std::vector<sg4::CommPtr> &pending_comms);

/*
 *	Generate result
 */
AssignedResult *generate_result(ProjectDatabaseValue &project, WorkunitT *workunit, int X);
