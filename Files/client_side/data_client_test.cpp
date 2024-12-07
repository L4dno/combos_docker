#include <gtest/gtest.h>
#include "data_client.hpp"
#include "components/shared.hpp"
#include <simgrid/s4u.hpp>
#include <string>
namespace sg4 = simgrid::s4u;

XBT_LOG_NEW_DEFAULT_CATEGORY(data_client_test, "The logging channel used in data_client testing");

void test_disk_access_function()
{
    int64_t size = 10000000;
    for_testing::disk_access_test(size);
    // 80000000 is the disk speed
    EXPECT_EQ(sg4::Engine::get_clock(), size / 80000000.0);
}

TEST(DataClient, DiskAccessFunction)
{
    int argc = 1;
    std::string program_name = "test_data_client";
    char *pr_n = program_name.data();
    sg4::Engine e(&argc, &pr_n);
    e.load_platform("../../Files/small_platform.xml");
    sg4::Actor::create("disk_access", e.host_by_name("Fafard"), &test_disk_access_function);
    e.run();
}

void test_ask_for_files_function()
{
    // prepare data structures
    ask_for_files_t ask_for_files_params = new s_ask_for_files_t();
    ask_for_files_params->project_number = 0;
    ask_for_files_params->project_priority = 10;

    group_t group_info = new s_group_t();
    ask_for_files_params->group_info = group_info;
    dclient_t dclient_info = new s_dclient_t();
    dclient_info->sum_priority = 20;
    dclient_info->total_storage = 100;
    dclient_info->server_name = "DC0";
    dclient_info->ask_for_files_mutex = sg4::Mutex::create();
    ask_for_files_params->dclient_info = dclient_info;
    ask_for_files_params->mailbox = "DC0_mb";

    SharedDatabase::_pdatabase.resize(1);
    auto &project = SharedDatabase::_pdatabase[0];
    project.output_file_storage = 0;
    project.ndata_clients = 1;
    project.data_clients.resize(1);
    project.ndata_client_servers = 1;
    project.data_client_servers.push_back("DCS0");
    project.ifcd_percentage = 100;
    project.ifgl_percentage = 100;
    project.dcreplication = 0;
    project.dcrfiles.resize(1);
    project.dcmutex = sg4::Mutex::create();
    project.input_file_size = 10000;
    project.rfiles_mutex = sg4::Mutex::create();

    g_rndg = std::make_unique<boost::random::rand48>(21);

    XBT_INFO("Test started");

    // call function that is tested
    auto test = sg4::Actor::create("data_client_ask_for_files_test", sg4::this_actor::get_host(), &for_testing::data_client_ask_for_files_test, ask_for_files_params);
    // ASSERT_EQ(1, 2);

    // mock side servers actions
    auto request_from_foo = sg4::Mailbox::by_name("DCS0")->get<dcsmessage>();
    ASSERT_FALSE(request_from_foo == nullptr);
    ASSERT_EQ((*request_from_foo).type, REQUEST);
    ASSERT_FALSE(((*request_from_foo).content) == nullptr);
    ASSERT_EQ(((dcsrequest_t)(*request_from_foo).content)->answer_mailbox, "DC0_mb");

    dcmessage_t answer = new s_dcmessage_t();
    answer->nworkunits = 1;
    WorkunitT workunit{.status = IN_PROGRESS, .ninput_files = 1, .input_files = {"DS0"}};
    answer->workunits["0"] = &workunit;
    answer->answer_mailbox = "DCS0";
    sg4::Mailbox::by_name("DC0_mb")->put(answer, 1);

    {
        // before it manages to get at the start of the cycle
        std::lock_guard lock(*dclient_info->ask_for_files_mutex);
        dclient_info->finish = true;
    }

    auto request_from_foo_to_ds = sg4::Mailbox::by_name("DS0")->get<dsmessage>();
    ASSERT_FALSE(request_from_foo_to_ds == nullptr);
    ASSERT_EQ(request_from_foo_to_ds->type, REQUEST);
    ASSERT_EQ(request_from_foo_to_ds->answer_mailbox, "DC0_mb");

    int *ok = new int(0);
    sg4::Mailbox::by_name("DC0_mb")->put(ok, 1);

    auto confirmation = sg4::Mailbox::by_name("DCS0")->get<s_dcsmessage_t>();
    ASSERT_FALSE(confirmation == nullptr);
    ASSERT_EQ(confirmation->type, REPLY);
    ASSERT_EQ(((dcsreply_t)confirmation->content)->dclient_name, "DC0");
    ASSERT_EQ(((dcsreply_t)confirmation->content)->workunits.size(), 1);

    test->join();
    ASSERT_EQ(project.dcrfiles[0], 1);
}
TEST(DataClient, AskForFilesFunction)
{
    int argc = 1;
    char *program_name = "test_data_client";
    sg4::Engine e(&argc, &program_name);
    e.load_platform("../../Files/small_platform.xml");
    sg4::Actor::create("disk_access", e.host_by_name("Fafard"), &test_ask_for_files_function);
    e.run();
}
