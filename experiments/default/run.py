# write parameters.xml file, gather output of simulator and save FLOPS into file for each hosts' power distribution
from pathlib import Path
import subprocess

experiment_dir = Path(__file__).parent

proj_name_1 = "RakeSearchtype1e15@home"
proj_name_2 = "RakeSearchtype2e13@home"


projects = [
f"""<sproject>
        <snumber>0</snumber>                <!-- Project number -->
        <name>{proj_name_1}</name>                <!-- Project name -->
        <nscheduling_servers>1</nscheduling_servers>    <!-- Number of scheduling servers -->
        <ndata_servers>1</ndata_servers>        <!-- Number of data servers of the project -->
        <ndata_client_servers>1</ndata_client_servers>  <!-- Number of data client servers -->
        <server_pw>12000000000f</server_pw>        <!-- Server power in FLOPS -->
        <disk_bw>167772160</disk_bw>            <!-- Disk speed in bytes/sec -->
        <ifgl_percentage>100</ifgl_percentage>           <!-- Percentage of input files generated locally -->
        <ifcd_percentage>100</ifcd_percentage>           <!-- Percentage of times clients must download new input files (they can't use old input files) -->
        <averagewpif>1</averagewpif>            <!-- Average number of workunits that share the same input files -->
        <input_file_size>1024</input_file_size>    <!-- Input file size in bytes -->
        <task_fpops>10000000000</task_fpops>        <!-- Task duration in flops -->
        <output_file_size>11000</output_file_size>    <!-- Answer size in bytes -->
        <min_quorum>1</min_quorum>            <!-- Number of times a workunit must be received in order to validate it-->
        <target_nresults>1</target_nresults>        <!-- Number of results to create initially -->
        <max_error_results>8</max_error_results>    <!-- Max number of erroneous results -->
        <max_total_results>10</max_total_results>    <!-- Max number of total results -->
        <max_success_results>8</max_success_results>    <!-- Max number of success results -->
        <delay_bound>64800</delay_bound>        <!-- Task deadline -->    
        <success_percentage>98</success_percentage>    <!-- Percentage of success results -->
        <canonical_percentage>84</canonical_percentage> <!-- Percentage of success results that make up a consensus -->
        <output_file_storage>0</output_file_storage>    <!-- Output file storage [0 -> data servers, 1 -> data clients] -->
        <dsreplication>1</dsreplication>        <!-- Files replication in data servers -->
        <dcreplication>1</dcreplication>        <!-- Files replication in data clients -->
    <sproject/>""",
f"""<sproject>
        <snumber>1</snumber>                <!-- Project number -->
        <name>{proj_name_2}</name>                <!-- Project name -->
        <nscheduling_servers>1</nscheduling_servers>    <!-- Number of scheduling servers -->
        <ndata_servers>1</ndata_servers>        <!-- Number of data servers of the project -->
        <ndata_client_servers>1</ndata_client_servers>  <!-- Number of data client servers -->
        <server_pw>12000000000f</server_pw>        <!-- Server power in FLOPS -->
        <disk_bw>167772160</disk_bw>            <!-- Disk speed in bytes/sec -->
        <ifgl_percentage>100</ifgl_percentage>           <!-- Percentage of input files generated locally -->
        <ifcd_percentage>100</ifcd_percentage>           <!-- Percentage of times clients must download new input files (they can't use old input files) -->
        <averagewpif>1</averagewpif>            <!-- Average number of workunits that share the same input files -->
        <input_file_size>1024</input_file_size>    <!-- Input file size in bytes -->
        <task_fpops>100000000000</task_fpops>        <!-- Task duration in flops -->
        <output_file_size>11000</output_file_size>    <!-- Answer size in bytes -->
        <min_quorum>1</min_quorum>            <!-- Number of times a workunit must be received in order to validate it-->
        <target_nresults>1</target_nresults>        <!-- Number of results to create initially -->
        <max_error_results>8</max_error_results>    <!-- Max number of erroneous results -->
        <max_total_results>10</max_total_results>    <!-- Max number of total results -->
        <max_success_results>8</max_success_results>    <!-- Max number of success results -->
        <delay_bound>64800</delay_bound>        <!-- Task deadline -->    
        <success_percentage>95</success_percentage>    <!-- Percentage of success results -->
        <canonical_percentage>99</canonical_percentage> <!-- Percentage of success results that make up a consensus -->
        <output_file_storage>0</output_file_storage>    <!-- Output file storage [0 -> data servers, 1 -> data clients] -->
        <dsreplication>1</dsreplication>        <!-- Files replication in data servers -->
        <dcreplication>1</dcreplication>        <!-- Files replication in data clients -->
    <sproject/>
"""
]

groups = ["""
<group>
        <n_clients>2</n_clients>            <!-- Number of clients of the group -->
        <ndata_clients>1</ndata_clients>        <!-- Number of data clients of the group -->
        <connection_interval>60</connection_interval>    <!-- Connection interval -->    
        <scheduling_interval>3600</scheduling_interval>    <!-- Scheduling interval -->
        <gbw>50Mbps</gbw>                <!-- Cluster link bandwidth in bps -->
        <glatency>7.3ms</glatency>            <!-- Cluster link latency -->
        
           
        <traces_file>/Traces/boom_host_cpu</traces_file>    <!-- Host power traces file-->

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
        
        

        <att_projs>2</att_projs>            <!-- Number of projects attached -->
        <gproject>
            <pnumber>0</pnumber>            <!-- Project number -->
            <priority>{0}</priority>            <!-- Project priority -->
            <lsbw>10Gbps</lsbw>            <!-- Link bandwidth (between group and scheduling servers) -->
            <lslatency>50us</lslatency>        <!-- Link latency (between group and scheduling servers) -->
            <ldbw>2Mbps</ldbw>            <!-- Link bandwidth (between group and data servers) -->
            <ldlatency>50us</latency>        <!-- Link latency (between group and data servers) -->    
        <gproject/>    
        <gproject>
            <pnumber>1</pnumber>            <!-- Project number -->
            <priority>{1}</priority>            <!-- Project priority -->
            <lsbw>10Gbps</lsbw>            <!-- Link bandwidth (between group and scheduling servers) -->
            <lslatency>50us</lslatency>        <!-- Link latency (between group and scheduling servers) -->
            <ldbw>2Mbps</ldbw>            <!-- Link bandwidth (between group and data servers) -->
            <ldlatency>50us</latency>        <!-- Link latency (between group and data servers) -->    
        <gproject/>    
    <group/>
"""
]

log_path = "<execute_state_log_path> " + str(experiment_dir.resolve()) + "/execute_stat_{2}.csv </execute_state_log_path>"
new_line = '\n'

file_template =  f"""
<simulation_time>4</simulation_time>                <!-- Simulation time in hours  -->
<warm_up_time>0</warm_up_time>                <!-- Warm up time in hours -->

<!-- Server side -->
<server_side>
    <n_projects>{len(projects)}</n_projects>                <!-- Number of projects -->
    {new_line.join(projects)}
<server_side/>

<client_side>
    <n_groups>{len(groups)}</n_groups>                    <!-- Number of groups -->
    {new_line.join(groups)}    
<client_side/>

<experiment_run>
    <timeline>
        {log_path}
        <observable_clients> c10,c16 </observable_clients> <!-- format is "c[cluster_ind][client_ind],c[cluster_ind][client_ind]". Cluster's index starts with 1, when client's - with 0 -->
    <timeline/>

    <seed_for_deterministic_run> 6523446 </seed_for_deterministic_run>

    <measures>
        <clean_before_write> true </clean_before_write>
        <save_filepath> {str(experiment_dir.resolve())}/save_file.txt </save_filepath>
    <measures/>
<experiment_run/>
"""

def run_in_schell(cmd: str):
    command = cmd
    try:
        result = subprocess.run(command, shell=True, check=True,
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
        # form parameters file and all linked
        with open("parameters.xml", "w") as wfile:
            print(file_template.format(priority1, priority2, f"{priority1}-{priority2}"), file=wfile)
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
