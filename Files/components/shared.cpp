#include "shared.hpp"

int g_total_number_clients = 1000;
int g_total_number_data_clients = 100;
int g_total_number_ordinary_clients = (g_total_number_clients - g_total_number_data_clients);
int MAX_SIMULATED_TIME = 100;
int WARM_UP_TIME = 20;

double maxtt = (MAX_SIMULATED_TIME + WARM_UP_TIME) * 3600;
double maxst = (MAX_SIMULATED_TIME) * 3600;
double maxwt = (WARM_UP_TIME) * 3600;

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

/*
 *	Generate result
 */
AssignedResult *generate_result(ProjectDatabaseValue &project, WorkunitT *workunit, int X)
{

    AssignedResult *result = new AssignedResult();
    result->workunit = workunit;
    result->ninput_files = workunit->ninput_files;
    result->input_files = workunit->input_files;
    project.ncurrent_results++;
    project.nresults++;

    // workunit->times[(int)workunit->ntotal_results++] = sg4::Engine::get_clock();
    workunit->times.push_back(sg4::Engine::get_clock());
    result->number = workunit->ntotal_results;
    workunit->ntotal_results++;

    if (X == 1)
        workunit->ncurrent_error_results--;

    // todo: у нас кто-нибудь ждет на wh_empty?...
    if (project.ncurrent_results >= 1)
        project.wg_empty->notify_all();

    return result;
}