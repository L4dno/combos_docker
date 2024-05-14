#include "shared.hpp"

int g_total_number_clients = 1000;
int g_total_number_data_clients = 100;
int g_total_number_ordinary_clients = (g_total_number_clients - g_total_number_data_clients);
int MAX_SIMULATED_TIME = 100;
int WARM_UP_TIME = 20;

double maxtt = (MAX_SIMULATED_TIME + WARM_UP_TIME) * 3600;
double maxst = (MAX_SIMULATED_TIME) * 3600;
double maxwt = (WARM_UP_TIME) * 3600;

ProjectDatabase SharedDatabase::_pdatabase = {};             // Initialize the static member
sserver_t SharedDatabase::_sserver_info = nullptr;           // Scheduling servers information
dserver_t SharedDatabase::_dserver_info = nullptr;           // Data servers information
dcserver_t SharedDatabase::_dcserver_info = nullptr;         // Data client servers information
std::vector<data_client> SharedDatabase::_dclient_info = {}; // Data clients information
std::vector<client_group> SharedDatabase::_group_info = {};  // C

std::unique_ptr<boost::rand48> g_rndg = nullptr;
std::unique_ptr<boost::rand48> g_rndg_for_host_speed = nullptr;
std::unique_ptr<boost::rand48> g_rndg_for_disk_cap = nullptr;
std::unique_ptr<boost::rand48> g_rndg_for_data_client_avail = nullptr;
std::unique_ptr<boost::rand48> g_rndg_for_client_avail = nullptr;

std::unordered_map<std::string, thermometer::Measure<double> *> g_measure_task_duration_per_project = {};
thermometer::Measure<double> *g_measure_non_availability_duration = nullptr;

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
void delete_completed_communications(sg4::ActivitySet &pending_comms)
{
    do
    {
        auto ready_comm_ptr = pending_comms.test_any();
        if (ready_comm_ptr == nullptr)
        {
            break;
        }
        pending_comms.erase(ready_comm_ptr);
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

// todo: right now we don't support more then 10 groups
bool the_same_client_group(const std::string &a, const std::string &b)
{
    return a[0] == 'd' || b[0] == 'd' || (a[0] == 'c' && b[0] == 'c' && a[1] == b[1]);
}