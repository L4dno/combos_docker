/****************** CREAR DEPLOYMENT.XML CON EL NUMERO DE CLIENTES PASADO COMO ARGUMENTO ********************/

#include <string>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <optional>
#include "parameters_struct_from_yaml.hpp"

class TraceParameter
{
public:
	TraceParameter(std::optional<std::string> traces_file, std::string use_for_what) : params_stream(traces_file.has_value() ? *traces_file : "")
	{
		if (params_stream.is_open())
		{
			std::cout << "use file for " << use_for_what << ": " << *traces_file << std::endl;
		}
		else
		{
			std::cout << "use distribution for " << use_for_what << ": " << std::endl;
			perror("can't open file");
		}
	}

	std::optional<double> get_param()
	{
		if (!params_stream.is_open())
		{
			return std::nullopt;
		}
		else
		{
			double result = -1;

			if (params_stream.eof())
			{
				params_stream.seekg(0);
			}

			params_stream >> result;

			return result;
		}
	}

private:
	std::ifstream params_stream;
};

void write_trace_parameter(FILE *fd, TraceParameter &generator, const std::string &nullopt_str)
{
	if (auto parameter_value = generator.get_param())
	{
		fprintf(fd, "        <argument value=\"%lf\"/>  ", *parameter_value);
	}
	else
	{
		fprintf(fd, "        <argument value=\"%s\"/>  ", nullopt_str.c_str());
	}
	fprintf(fd, "\n");
}

int main(int argc, char *argv[])
{
	auto config = parameters::read_from_file(argv[1]);

	int l = 0;
	int64_t index = 1;
	FILE *fd;

	std::string result_file = "deployment.xml";
	const char *env_p;
	if (env_p = std::getenv("PROJECT_SOURCE_DIR"))
	{
		result_file = std::string(env_p) + "/Files/" + result_file;
	}

	int scheduling_server_number = 0, data_client_server_number = 0;
	int data_clients = 0;
	/* Usage
	if (argc < 2) {
			printf("Usage: %s n_clusters\n", argv[0]);
			exit(1);
	} */

	/* *********** DEPLOYMENT.XML *************/
	fd = fopen(result_file.c_str(), "w");
	if (fd == NULL)
	{
		printf("Error opening deployment file!\n");
		exit(1);
	}

	/* BASICS */
	fprintf(fd, "<?xml version='1.0'?>\n");
	fprintf(fd, "<!DOCTYPE platform SYSTEM \"https://simgrid.org/simgrid.dtd\">\n");
	fprintf(fd, "<platform version=\"4.1\">\n");

	fprintf(fd, "   <actor host=\"r0\" function=\"show_progress\" />");
	fprintf(fd, "\n");

	int n_projects = config.server_side.n_projects;

	for (int i = 0; i < n_projects; i++)
	{
		auto project = config.server_side.sprojects[i];
		fprintf(fd, "   <actor host=\"b%d\" function=\"init_database\"> ", project.snumber);
		fprintf(fd, "\n");
		fprintf(fd, "           <argument value=\"%d\"/> ", project.snumber); // Numero del proyecto
		fprintf(fd, "\n");
		fprintf(fd, "           <argument value=\"%s\"/> ", project.name.c_str()); // Nombre de proyecto
		fprintf(fd, "\n");
		fprintf(fd, "           <argument value=\"%d\"/> ", project.output_file_size); // Tamanyo de respuesta - 64 KB
		fprintf(fd, "\n");
		fprintf(fd, "           <argument value=\"%lld\"/> ", project.task_fpops); // Duracion de workunit (en flops)
		fprintf(fd, "\n");
		fprintf(fd, "           <argument value=\"%d\"/> ", project.ifgl_percentage); // Percentage of input files generated locally
		fprintf(fd, "\n");
		fprintf(fd, "           <argument value=\"%d\"/> ", project.ifcd_percentage); // Percentage of times a client must download new input files (they can't use previous ones)
		fprintf(fd, "\n");
		fprintf(fd, "           <argument value=\"%d\"/> ", project.averagewpif); // Average workunits per input files
		fprintf(fd, "\n");
		fprintf(fd, "           <argument value=\"%d\"/> ", project.min_quorum); // Quorum
		fprintf(fd, "\n");
		fprintf(fd, "           <argument value=\"%d\"/> ", project.target_nresults); // target_nresults
		fprintf(fd, "\n");
		fprintf(fd, "           <argument value=\"%d\"/> ", project.max_error_results); // max_error_results
		fprintf(fd, "\n");
		fprintf(fd, "           <argument value=\"%d\"/> ", project.max_total_results); // max_total_results
		fprintf(fd, "\n");
		fprintf(fd, "           <argument value=\"%d\"/> ", project.max_success_results); // max_success_results
		fprintf(fd, "\n");
		fprintf(fd, "           <argument value=\"%lld\"/> ", project.delay_bound); // Deadline de workunit
		fprintf(fd, "\n");
		fprintf(fd, "           <argument value=\"%d\"/> ", project.input_file_size); // Tamanyo de workunit - 360 KB
		fprintf(fd, "\n");
		fprintf(fd, "           <argument value=\"%d\"/> ", project.disk_bw); // Disk speed
		fprintf(fd, "\n");
		fprintf(fd, "           <argument value=\"%d\"/> ", project.ndata_servers); // Number of data servers
		fprintf(fd, "\n");
		fprintf(fd, "           <argument value=\"%d\"/> ", project.output_file_storage); // Output files storage
		fprintf(fd, "\n");
		fprintf(fd, "           <argument value=\"%d\"/> ", project.dsreplication); // Files replication in data servers
		fprintf(fd, "\n");
		fprintf(fd, "           <argument value=\"%d\"/> ", project.dcreplication); // Files replication in data clients
		fprintf(fd, "\n");
		fprintf(fd, "   </actor> ");
		fprintf(fd, "\n");

		fprintf(fd, "   <actor host=\"b%d\" function=\"work_generator\"> ", project.snumber);
		fprintf(fd, "\n");
		fprintf(fd, "           <argument value=\"%d\"/> ", project.snumber); // Numero del proyecto
		fprintf(fd, "\n");
		fprintf(fd, "   </actor> ");
		fprintf(fd, "\n");
		fprintf(fd, "   <actor host=\"b%d\" function=\"validator\" >", project.snumber);
		fprintf(fd, "\n");
		fprintf(fd, "           <argument value=\"%d\"/> ", project.snumber); // Numero del proyecto
		fprintf(fd, "\n");
		fprintf(fd, "   </actor> ");
		fprintf(fd, "\n");
		fprintf(fd, "   <actor host=\"b%d\" function=\"assimilator\" >", project.snumber);
		fprintf(fd, "\n");
		fprintf(fd, "           <argument value=\"%d\"/> ", project.snumber); // Numero del proyecto
		fprintf(fd, "\n");
		fprintf(fd, "   </actor> ");
		fprintf(fd, "\n");

		// Scheduling servers
		for (int j = 0; j < project.nscheduling_servers; j++)
		{
			fprintf(fd, "   <actor host=\"s%d%d\" function=\"scheduling_server_requests\"> ", i + 1, j);
			fprintf(fd, "\n");
			fprintf(fd, "           <argument value=\"%d\"/> ", project.snumber); // Numero del proyecto
			fprintf(fd, "\n");
			fprintf(fd, "           <argument value=\"%d\"/> ", scheduling_server_number); // Numero del servidor
			fprintf(fd, "\n");
			fprintf(fd, "   </actor> ");
			fprintf(fd, "\n");

			fprintf(fd, "   <actor host=\"s%d%d\" function=\"scheduling_server_dispatcher\"> ", i + 1, j);
			fprintf(fd, "\n");
			fprintf(fd, "           <argument value=\"%d\"/> ", project.snumber); // Numero del proyecto
			fprintf(fd, "\n");
			fprintf(fd, "           <argument value=\"%d\"/> ", scheduling_server_number++); // Numero del servidor
			fprintf(fd, "\n");
			fprintf(fd, "   </actor> ");
			fprintf(fd, "\n");
		}

		// Data servers
		for (int j = 0; j < project.ndata_servers; j++, l++)
		{
			fprintf(fd, "   <actor host=\"d%d%d\" function=\"data_server_requests\"> ", i + 1, j);
			fprintf(fd, "\n");
			fprintf(fd, "           <argument value=\"%d\"/> ", l);
			fprintf(fd, "\n");
			fprintf(fd, "   </actor> ");
			fprintf(fd, "\n");

			fprintf(fd, "   <actor host=\"d%d%d\" function=\"data_server_dispatcher\"> ", i + 1, j);
			fprintf(fd, "\n");
			fprintf(fd, "           <argument value=\"%d\"/> ", l);
			fprintf(fd, "\n");
			fprintf(fd, "           <argument value=\"%d\"/> ", project.snumber);
			fprintf(fd, "\n");
			fprintf(fd, "   </actor> ");
			fprintf(fd, "\n");
		}

		// Data client servers
		for (int j = 0; j < project.ndata_client_servers; j++)
		{
			fprintf(fd, "   <actor host=\"t%d%d\" function=\"data_client_server_requests\"> ", i + 1, j);
			fprintf(fd, "\n");
			fprintf(fd, "           <argument value=\"%d\"/> ", project.snumber); // Numero del proyecto
			fprintf(fd, "\n");
			fprintf(fd, "           <argument value=\"%d\"/> ", data_client_server_number); // Numero del servidor
			fprintf(fd, "\n");
			fprintf(fd, "   </actor> ");
			fprintf(fd, "\n");

			fprintf(fd, "   <actor host=\"t%d%d\" function=\"data_client_server_dispatcher\"> ", i + 1, j);
			fprintf(fd, "\n");
			fprintf(fd, "           <argument value=\"%d\"/> ", project.snumber); // Numero del proyecto
			fprintf(fd, "\n");
			fprintf(fd, "           <argument value=\"%d\"/> ", data_client_server_number++); // Numero del servidor
			fprintf(fd, "\n");
			fprintf(fd, "   </actor> ");
			fprintf(fd, "\n");
		}
	}

	int n_clusters = config.client_side.n_groups;

	/* PRINT CLIENTS*/
	for (int i = 0; i < n_clusters; i++)
	{
		auto group = config.client_side.groups[i];

		int n_clients = group.n_clients;
		int ndata_clients = group.ndata_clients;
		int att_projs = group.att_projs;

		std::string traces_file_dir = std::string(env_p).size() == 0 ? std::string("../") : std::string(env_p);

		auto construct_file_name = [&traces_file_dir](const std::optional<std::string> &trace_file)
		{
			return trace_file.has_value() ? std::optional(traces_file_dir + *trace_file) : std::nullopt;
		};

		std::optional<std::string> host_power_traces_file = construct_file_name(group.traces_file);
		std::optional<std::string> disk_capacity_traces_file = construct_file_name(group.db_traces_file);

		TraceParameter host_power_generator(host_power_traces_file, "for host power");
		TraceParameter disk_capacity_generator(disk_capacity_traces_file, "for host's disk capacity");

		int j;
		for (j = 0; j < n_clients - ndata_clients; j++)
		{
			if (j == 0)
			{
				fprintf(fd, "   <actor host=\"c%d%d\" function=\"client\"> ", i + 1, j);
				fprintf(fd, "\n");
				fprintf(fd, "        <argument value=\"%d\"/>  ", i); // <!-- Cluster number-->
				fprintf(fd, "\n");
				fprintf(fd, "        <argument value=\"%d\"/>  ", n_clients); // <!-- Number of clients -->
				fprintf(fd, "\n");
				fprintf(fd, "        <argument value=\"%d\"/>  ", n_clients - ndata_clients); // <!-- Number of ordinary clients -->
				fprintf(fd, "\n");
				fprintf(fd, "        <argument value=\"%d\"/>  ", group.connection_interval); // <!-- ConnectionInterval -->
				fprintf(fd, "\n");
				fprintf(fd, "        <argument value=\"%d\"/>  ", group.scheduling_interval); // <!-- SchedulingInterval -->
				fprintf(fd, "\n");
				fprintf(fd, "        <argument value=\"%lf\"/>  ", group.max_speed); // <!-- Max speed -->
				fprintf(fd, "\n");
				fprintf(fd, "        <argument value=\"%lf\"/>  ", group.min_speed); // <!-- Min speed -->
				fprintf(fd, "\n");
				fprintf(fd, "        <argument value=\"%d\"/>  ", group.pv_distri); // <!-- Sp. random distribution -->
				fprintf(fd, "\n");
				fprintf(fd, "        <argument value=\"%f\"/>  ", group.pa_param); // <!-- A argument -->
				fprintf(fd, "\n");
				fprintf(fd, "        <argument value=\"%f\"/>  ", group.pb_param); // <!-- B argument -->
				fprintf(fd, "\n");
				fprintf(fd, "        <argument value=\"%d\"/>  ", group.db_distri); // <!-- Db. random distribution -->
				fprintf(fd, "\n");
				fprintf(fd, "        <argument value=\"%f\"/>  ", group.da_param); // <!-- A argument -->
				fprintf(fd, "\n");
				fprintf(fd, "        <argument value=\"%f\"/>  ", group.db_param); // <!-- B argument -->
				fprintf(fd, "\n");
				fprintf(fd, "        <argument value=\"%d\"/>  ", group.av_distri); // <!-- Av. random distribution -->
				fprintf(fd, "\n");
				fprintf(fd, "        <argument value=\"%f\"/>  ", group.aa_param); // <!-- A argument -->
				fprintf(fd, "\n");
				fprintf(fd, "        <argument value=\"%f\"/>  ", group.ab_param); // <!-- B argument -->
				fprintf(fd, "\n");
				fprintf(fd, "        <argument value=\"%d\"/>  ", group.nv_distri); // <!-- Nav. random distribution -->
				fprintf(fd, "\n");
				fprintf(fd, "        <argument value=\"%s\"/>  ", group.na_param.c_str()); // <!-- A argument -->
				fprintf(fd, "\n");
				fprintf(fd, "        <argument value=\"%s\"/>  ", group.nb_param.c_str()); // <!-- B argument -->
				fprintf(fd, "\n");

				write_trace_parameter(fd, host_power_generator, parameters::no_set_host_power); // <!-- Host power -->

				fprintf(fd, "        <argument value=\"%d\"/>", att_projs); // <!-- Number of projects attached -->
				fprintf(fd, "\n");
				for (int k = 0; k < att_projs; k++)
				{
					auto attached_project = group.gprojects[k];
					fprintf(fd, "        <argument value=\"Project%d\"/>", attached_project.pnumber + 1); // <!-- Project name ->
					fprintf(fd, "\n");
					fprintf(fd, "        <argument value=\"%d\"/>", attached_project.pnumber); // <!-- Project number -->
					fprintf(fd, "\n");
					fprintf(fd, "        <argument value=\"%d\"/>", attached_project.priority); // <!-- Project priority -->
					fprintf(fd, "\n");

					fprintf(fd, "        <argument value=\"%d\"/>", attached_project.success_percentage); // <!-- Project priority -->
					fprintf(fd, "\n");
					fprintf(fd, "        <argument value=\"%d\"/>", attached_project.canonical_percentage); // <!-- Project priority -->
					fprintf(fd, "\n");
				}
			}
			else
			{
				fprintf(fd, "   <actor host=\"c%d%d\" function=\"client\"> ", i + 1, j);
				fprintf(fd, "\n");
				fprintf(fd, "        <argument value=\"%d\"/>  ", i); // <!-- Cluster number-->
				fprintf(fd, "\n");
				write_trace_parameter(fd, host_power_generator, parameters::no_set_host_power); // <!-- Host power -->
			}

			fprintf(fd, "   </actor> ");
			fprintf(fd, "\n");
		}

		for (; j < n_clients; j++)
		{
			fprintf(fd, "   <actor host=\"c%d%d\" function=\"data_client_requests\"> ", i + 1, j);
			fprintf(fd, "\n");
			fprintf(fd, "           <argument value=\"%d\"/> ", i); // Cluster number
			fprintf(fd, "\n");
			fprintf(fd, "           <argument value=\"%d\"/> ", data_clients); // Data client number
			fprintf(fd, "\n");
			write_trace_parameter(fd, disk_capacity_generator, parameters::no_set_disk_capacity); // <!-- Disk capacity -->

			fprintf(fd, "   </actor> ");
			fprintf(fd, "\n");

			fprintf(fd, "   <actor host=\"c%d%d\" function=\"data_client_dispatcher\"> ", i + 1, j);
			fprintf(fd, "\n");
			fprintf(fd, "           <argument value=\"%d\"/> ", i); // Cluster number
			fprintf(fd, "\n");
			fprintf(fd, "           <argument value=\"%d\"/> ", data_clients++); // Data client number
			fprintf(fd, "\n");
			fprintf(fd, "   </actor> ");
			fprintf(fd, "\n");
		}
	}

	/* END */
	fprintf(fd, "</platform>\n");

	fclose(fd);

	return 0;
}
