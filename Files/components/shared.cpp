#include "shared.hpp"

ProjectDatabase SharedDatabase::_pdatabase = {};     // Initialize the static member
sserver_t SharedDatabase::_sserver_info = nullptr;   // Scheduling servers information
dserver_t SharedDatabase::_dserver_info = nullptr;   // Data servers information
dcserver_t SharedDatabase::_dcserver_info = nullptr; // Data client servers information
dclient_t SharedDatabase::_dclient_info = nullptr;   // Data clients information
group_t SharedDatabase::_group_info = nullptr;       // C

/*
 *	 Server compute simulation. Wait till the end of a executing task
 */
void compute_server(int flops)
{
    sg4::this_actor::execute(flops);
}

/*
 *	Blank result
 */
AssignedResult *blank_result()
{
    AssignedResult *result = new AssignedResult();
    result->workunit = NULL;  // Associated workunit
    result->ninput_files = 0; // Number of input files
    // result->input_files.clear(); // Input files names (URLs)
    result->number_tasks = 0; // Number of tasks (usually one)
    // result->tasks;            // Tasks
    return result;
}

/*
 * to free memory, we delete all completed asynchronous communications. It shouldn't affect the clocks in the simmulator.
 */
void delete_completed_communications(std::vector<sg4::CommPtr> &pending_comms)
{
    do
    {
        ssize_t ready_comm_ind = sg4::Comm::test_any(pending_comms);
        if (ready_comm_ind == -1)
        {
            break;
        }
        swap(pending_comms.back(), pending_comms[ready_comm_ind]);
        pending_comms.pop_back();
    } while (!pending_comms.empty());
}