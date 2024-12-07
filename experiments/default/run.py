# write parameters.xml file, gather output of simulator and save FLOPS into file for each hosts' power distribution
from pathlib import Path
import subprocess
import copy

experiment_dir = Path(__file__).parent

proj_name_1 = "RakeSearchtype1e15@home"
proj_name_2 = "RakeSearchtype2e13@home"

class Project:
    def __init__(self, **kwargs):
        self.__dict__.update(kwargs)

    def __getattr__(self, name):
        return self.__dict__.get(name, None)

    def Serialize(self):
        return f"""<sproject>
                <snumber>{self.snumber or 0}</snumber>                <!-- Project number -->
                <name>{self.name or "project1"}</name>                <!-- Project name -->
                <nscheduling_servers>{self.nscheduling_servers or 1}</nscheduling_servers>    <!-- Number of scheduling servers -->
                <ndata_servers>{self.ndata_servers or 1}</ndata_servers>        <!-- Number of data servers of the project -->
                <ndata_client_servers>{self.ndata_client_servers or 1}</ndata_client_servers>  <!-- Number of data client servers -->
                <server_pw>{self.server_pw or "12000000000f"}</server_pw>        <!-- Server power in FLOPS -->
                <disk_bw>{self.disk_bw or 167772160}</disk_bw>            <!-- Disk speed in bytes/  -->
                <ifgl_percentage>{self.ifgl_percentage or 100}</ifgl_percentage>           <!-- Percentage of input files generated locally -->
                <ifcd_percentage>{self.ifcd_percentage or 100}</ifcd_percentage>           <!-- Percentage of times clients must download new input files (they can't use old input files) -->
                <averagewpif>{self.averagewpif or 1}</averagewpif>            <!-- Average number of workunits that share the same input files -->
                <input_file_size>{self.input_file_size or 1024}</input_file_size>    <!-- Input file size in bytes -->
                <task_fpops>{self.task_fpops or 10000000000}</task_fpops>        <!-- Task duration in flops -->
                <output_file_size>{self.output_file_size or 11000}</output_file_size>    <!-- Answer size in bytes -->
                <min_quorum>{self.min_quorum or 1}</min_quorum>            <!-- Minimum quorum for a result to be considered valid -->
                <target_nresults>{self.target_nresults or 1}</target_nresults>
                <max_error_results>{self.max_error_results or 8}</max_error_results> <!-- Maximum number of error results -->
                <max_total_results>{self.max_total_results or 10}</max_total_results>  <!-- Maximum number of results -->
                <max_success_results>{self.max_success_results or 8}</max_success_results> <!-- Maximum number of successful results -->
                <delay_bound>{self.delay_bound or 64800}</delay_bound>            <!-- Maximum delay for a result -->
                <output_file_storage>{self.output_file_storage or 0}</output_file_storage>    <!-- Output file storage in bytes -->
                <dsreplication>{self.dsreplication or 1}</dsreplication>        <!-- Data server replication factor -->
                <dcreplication>{self.dcreplication or 1}</dcreplication>        <!-- Data client replication factor -->
                <sproject/>
                """

class GroupProject:
    def __init__(self, **kwargs):
        self.__dict__.update(kwargs)


    def __getattr__(self, name):
        return self.__dict__.get(name, None)
    
    def Serialize(self):
        return f"""
        <gproject>
            <pnumber>{self.pnumber or 0}</pnumber>            <!-- Project number -->
            <priority>{self.priority or 10}</priority>            <!-- Project priority -->
            <lsbw>{self.lsbw or "10Gbps"}</lsbw>            <!-- Link bandwidth (between group and scheduling servers) -->
            <lslatency>{self.lslatency or "50us"} </lslatency>        <!-- Link latency (between group and scheduling servers) -->
            <ldbw>{self.ldbw or "2Mbps"} </ldbw>            <!-- Link bandwidth (between group and data servers) -->
            <ldlatency>{self.ldlatency or "50us"}</latency>        <!-- Link latency (between group and data servers) -->    
        
            <success_percentage>{self.success_percentage or 98}</success_percentage> <!-- Percentage of successful results -->
            <canonical_percentage>{self.canonical_percentage or 84}</canonical_percentage> <!-- Percentage of canonical results -->
                
        
        <gproject/>
        """
        

class Group:
    def __init__(self, **kwargs):
        self.__dict__.update(kwargs)
        self.gprojects = []

    def __getattr__(self, name):
        return self.__dict__.get(name, None)
    
    def set_project(self, project: GroupProject):
        self.gprojects.append(project)

    def Serialize(self):
        result = f"""
<group>
        <n_clients>{self.n_clients or 10}</n_clients>            <!-- Number of clients of the group -->
        <ndata_clients>{self.ndata_clients or 1}</ndata_clients>        <!-- Number of data clients of the group -->
        <connection_interval>{self.connection_interval or 60}</connection_interval>    <!-- Connection interval -->    
        <scheduling_interval>{self.scheduling_interval or 3600}</scheduling_interval>    <!-- Scheduling interval -->
        <gbw>{self.gbw or "50Mbps"}</gbw>                <!-- Cluster link bandwidth in bps -->
        <glatency>{self.glatency or "7.3ms"}</glatency>            <!-- Cluster link latency -->
        
           
        <traces_file>{self.traces_file or "/Traces/boom_host_cpu"}</traces_file>    <!-- Host power traces file-->

        <max_speed>117.71</max_speed>            <!-- Maximum host speed in GFlops -->
        <min_speed>0.07</min_speed>            <!-- Minumum host speed in GFlops -->
        <pv_distri>5</pv_distri>            <!-- Speed fit distribution [ran_weibull, ran_gamma, ran_lognormal, normal, hyperx, exponential] -->
        <pa_param>0.0534</pa_param>            <!-- A -->
        <pb_param>-1</pb_param>                <!-- B -->

        <db_traces_file>/Traces/storage/boom_disk_capacity</db_traces_file>    <!-- Host power traces file -->
        <db_distri>2</db_distri>            <!-- Disk speed fit distribution [ran_weibull, ran_gamma, ran_lognormal, normal, hyperx, exponential] -->
        <da_param>21.086878916910564</da_param>            <!-- A -->
        <db_param>159.1597190181666</db_param>                <!-- B -->
        
        <av_distri>1</av_distri>            <!-- Availability fit distribution [ran_weibull, ran_gamma, ran_lognormal, normal, hyperx, exponential] -->
        <aa_param>0.357</aa_param>            <!-- A -->
        <ab_param>43.652</ab_param>            <!-- B -->

        <nv_distri>8</nv_distri>            <!-- Non-availability fit distribution [ran_weibull, ran_gamma, ran_lognormal, normal, hyperx, exponential, 8=3-phase hyperx] -->
        <na_param>0.338,0.390,0.272</na_param>            <!-- A, devided by dots if there are many -->
        <nb_param>0.029,30.121,1.069</nb_param>            <!-- B, devided by dots if there are many -->
        """
        result += f"<att_projs>{len(self.gprojects)}</att_projs>            <!-- Number of projects attached -->"

        for project in self.gprojects:
            result += project.Serialize()
        return result + "<group/>\n"

class ConfigurationFile:
    def __init__(self, **kwargs):
        self.__dict__.update(kwargs)
        self.projects = []
        self.clusters = []
    
    def add_project(self, project: Project):
        self.projects.append(project)
    def add_cluster(self, cluster: Group):
        self.clusters.append(cluster)


    def __getattr__(self, name):
        return self.__dict__.get(name, None)
    def Serialize(self):
        log_path = "<execute_state_log_path> " + str(experiment_dir.resolve()) + "/execute_stat_{2}.csv </execute_state_log_path>"
        result = f"""
<simulation_time>{self.simulation_time or 4}</simulation_time>                <!-- Simulation time in hours  -->
<warm_up_time>{self.warm_up_time or 0}</warm_up_time>                <!-- Warm up time in hours -->
<!-- Server side -->
<server_side>
    <n_projects>{len(self.projects)}</n_projects>                <!-- Number of projects -->
"""
        for project in self.projects:
            result += project.Serialize()
        result += f"<server_side/>\n<client_side>\n     <n_groups>{len(self.clusters)}</n_groups>                    <!-- Number of groups -->\n"

        for cluster in self.clusters:
            result += cluster.Serialize()
        result += f"""<client_side/>\n<experiment_run>

    <seed_for_deterministic_run> {self.seed_for_deterministic_run or 6523446} </seed_for_deterministic_run>

    <measures>
        <clean_before_write> true </clean_before_write>
        <save_filepath> {str(experiment_dir.resolve())}/save_file.txt </save_filepath>
    <measures/>
<experiment_run/>
"""
        return result

def run_in_schell(cmd: str):
    command = cmd
    try:
        result = subprocess.run(command, shell=True, check=False,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        print(result.stderr)
        return result.stdout
    except subprocess.CalledProcessError as e:
        raise e


# Results failed:               86 (1.0%)
# Workunits error:              0 (0.0%)

SUM_PRIORITIES = 5

key_to_look_at = ['Workunits valid:']
REPEATING_PER_CONFIG = 1
with open(experiment_dir / "result.csv", "w") as fout:
    print("label,proj1_nvalid_result,proj2_nvalid_result", file=fout)

    for priority1 in range(1, SUM_PRIORITIES):
        priority2 = SUM_PRIORITIES - priority1
        print("priority1", priority1, priority2)

        # form configuration file
        config = ConfigurationFile(simulation_time = 48)
        config.add_project(Project(name=proj_name_1, snumber= 0))
        config.add_project(Project(name=proj_name_2, snumber=1))
        cluster = Group()
        cluster.set_project(GroupProject(priority=priority1, pnumber=0))
        config.add_cluster(cluster)
        cluster.set_project(GroupProject(priority=priority2, pnumber=1))
        
        cluster1 = Group(n_clients = 100)
        cluster1.set_project(GroupProject(priority=priority1, pnumber=0))
        cluster1.set_project(GroupProject(priority=priority2, pnumber=1))
        config.add_cluster(cluster1)

        # form parameters file and all linked
        with open("parameters.xml", "w") as wfile:
            print(config.Serialize(), file=wfile)
        run_in_schell("./generator")


        # run REPEATING_PER_CONFIG times and save in file.
        # in ./generator they shuffle file with host power, so it
        # must be included here, no above when using traces_file
        for i in range(REPEATING_PER_CONFIG):
            print(i)
            get_macro_stat = run_in_schell("./execute")
            
            proj_name = None
            extract_result = {}

            for line in get_macro_stat.split('\n'):
                if proj_name_1 in line:
                    proj_name = proj_name_1
                elif proj_name_2 in line:
                    proj_name = proj_name_2
                if key_to_look_at[0] not in line:
                    continue 
                #     Results valid:                910 (80.7%)
                get_stat = line.strip().split(key_to_look_at[0])[1].strip().split(' ')[0].replace(',', '')
                #     Results valid:                910 (80.7%)
                # get_stat = line.strip().split('(')[1].strip().split('%)')[0].replace(',', '')
                extract_result[proj_name] = get_stat
            print(f"{priority1}-{priority2},{extract_result[proj_name_1]},{extract_result[proj_name_2]}", file=fout)
