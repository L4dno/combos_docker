# combos

# Installation
You need 
1. CMake 3.11 or later
2. Boost 1.82, __context__ component
3. Clone this repository 
4. Create the directory __build/__ and run "cmake .." from it.
5. Build the project via "make". SimGrid will be automatically downloaded and built, so it can take time during the first run of the command.

Feel free to write to me if you have problems with this.

# Usage
The file __generator__ is already included in the project. Run it and then __./execute__ which is generated after the first command. Configuration is in the __parameters.xml__. Please follow the format as the positions of backslashes are important. I plan to migrate from xml to yaml, but it's not done yet as not critical.

You also can look inside the directory __experiments/default/__ to modify __run.py__ and run experiments in a more convenient way than just save several __parameters.xml__. More examples will appear in the future.

# Tests
To write tests, you need to build the project (via __make__) and then go to the __build/Files__ and run __ctest__. In case of failure, you can turn the logs on (example in the __boinc.cpp__, ```xbt_log_control_set```).

The initial project didn't have tests at all, and I started to add them only recently, so there is much code uncovered. You are welcome to contribute. It's my main direction of work for the nearest future.


# Background


The main difference from the original repository is that the code which simulates BOINC was switched from __boinc_simulator.c__ to __boinc.cpp__. The reason: SimGrid's newest version is 3.34, when combos compiles only with 3.11. I've tried several versions as well and attempted to play with combos + 3.11v by changing __parameters.xml__. However, it either didn't compile or failed with a segmentation fault. So, the first attempt was only to switch the file's extension and fix all compilation errors.

Besides the aforementioned reasons, the code in c requires a careful work with pointers. c++ provides a little bit more guarantees. At least it will shout.

Apparently, the new code generates results that are significantly different from the original version if I run them with the original __parameters.xml__. Even after I fixed all suspicious places I'd found in the old code, nothing changed. The new version gives about 56 GFlops and the old one - about 220 GFlops in average. It looks like the problem is in simgrid and only because of the engine's work the old code produces more tasks or more frequently.

At some moment, I gave up and decoupled new version from the old one, what resulted in working only under c++ code and a plan to reproduce the "Validation of the simulator" section from the article about combos.

To understand the code and generally be able to alter it, I've split 4000 loc into several files. __components__ is about server side and __client_side__ is about clients. (Naming could be changed). After I finish the work on another branch, there will be explanations what each component or part is about.

I thought I could finally relax as I no longer saw SegFault, if there wasn't another nuance. I tried to add a new project to __parameters.xml__ and program failed once again. After all, I added __xml2yaml.py__ that converts __parameters.xml__ into __parameters.yaml__ and pass it to the program. It all works but there is advice to alter __parameters.xml__ carefully, keeping the same format of tags as it has now. To proof the correctness, I ran script that summarizes outputs of the programs' versions and calculates absolute and relative errors. The result is in __compare_results.json__. The relative errors are small (<= 7% in major) expect for "DC. Number of downloads from data server 1" but it has small values so it's okay. 

# Difference with boinc

- After digging in the code, I noticed the difference with boinc approach. In boinc clients first ask scheduling server to get work and after that might download files. In combos they first download and replicate all input files they need and only then scheduling server can assign tasks to clients. Upd: or the same. somewhere I saw that boinc described the same logic
- In combos they use debt what must be depricated in the boinc. According to wiki scheduling priorities were introduced.
- Combos is more about clusters and they even can have \[data clients\] specially for input files.
- combos simulate only 1-cpu hosts

# Changes

```using sg4 = simgrid::s4u```

- ```xbt_swag``` -> ```boost::intrusive::list```
- ```xbt_structure``` -> ```std```
- ```char*```, ```type*``` -> ```std::string``` + ```std::vector```
- ```xbt_mutex/cond_var``` -> ```sg4::MutexPtr``` + ```s4u::ConditionVariablePtr```
- ```msg_process_t``` -> ```sg4::ActorPtr```
- ```MSG_task_send/MSG_task_receive``` + ```msg_task_t``` + ```msg_comm_t``` -> ```sg4::Mailbox *``` + ```sg4::ExecPtr``` + ```sg4::CommPtr```
- add ```number_past_through_assimilator``` field in ```workunit```. Sorry, not sure about correctness of the naming. I had a workunit had been deleted before validator or assimilator finished to work with them. In the old code it was UB.
- change work with mutex a little - there were double acquires and continues releases. Well, I'm not confident if the cases happened when it could affect anything. I'm not sure if it actually didn't affect anything.
- there were problems with system (deadlocks or exceptions) when simulation finished. Now it has to be fixed.
- tasks' deadlines wer calculated as delay_bound + creation time, not delay_bound + sent time.
- if a project didn't have work to do clients could freeze and never ever sent results or requested tasks. 
- add several random generators, so that if during experiments you vary one part of the system and set seeds manually, other parts of the system remain deterministic. Previously, the generator was single and different ```gproject/priority``` parameters caused different availability periods, what was undesirable. 
- suspend and resume msg_task when simulate down time of a client. Previously, a running task continues to be executed even if we simulated non-availability period.
- there can be two types of hosts - ones that compute results with a few errors and ones with a lot of erros. Now it's possible to setup two clusters with different amount of erroneous results (see __parameters.xml__)

# Модификации
- Проект переведен на С++, версия SimGrid - на последнюю,основной файл был разбит на несколько компонент.
- Добавлено поле number_past_through_assimilator в workunit. Был workunit, который был удален до того, как валидатор или ассимилятор закончили с ними работу. В старом коде это была UB.
- Дедлайн выполнения задач сначала рассчитывались как допустимая задержка + время создания, а не допустимая задержка + время отправленния на клиент.
- В случае, если у проекта не было работы для выполнения, клиенты могли заморозиться и никогда не отправить результаты или запросить задачи.
- Возможны два типа хостов - те, которые вычисляют результаты с небольшим количеством ошибок и те, у которых много ошибок. Теперь возможно настроить два кластера с разным количеством ошибочных результатов. Раньше это было невозможно.
- Вместо argc, argv акторам передается конфиг, распарсенный из yaml. Стало удобно добавлять и править параметры в конфигурационном файле

# Work yet to be done
- I close eyes on freeing resources. They might leak a lot. Better naming is also under "eventual consideration"
- I've run valgrind many times to fix UB. ~~The last time the output wasn't clean yet, so fixes yet to be done.~~ The last time, it was clean (except leaks). Still, it's worth checking more. 
- ~~I'm not sure about how an asynchronous communication works with a synchronous one in this project. I would like to spend time to understand it better.~~
- ~~there is a piece of code ```workunit->times[reply->result_number]``` in the original version. In my current understanding, a size of ```workunit->times``` correlates to ```results```, when ```reply->result_number``` is proportional to ```tasks```, so I've got ```out_of_range exception```.~~
- ~~my intrusive lists are structure with ```task-s``` as members. Probably the correct way is to keep pointers ```task_t-s```.~~
- I'm not sure that code is working with several clusters
- When data clients ask for input files from data_client servers, one "thread" is working for each project. If there are several projects, "thread"s won't communicate and just keep asking for files while they have free space in their dedicated memory.
- redo evaluation in a similar way as in the combos article

# Warning
If you set up several projects and get in the end
```bash
[c1320:Project1:c1320
:(2033) 7200.000000] ./src/kernel/actor/ActorImpl.cpp:263: [root/CRITICAL] Gasp! This exception may be lost by subsequent calls.
Backtrace (displayed in actor Project1:c1320
```
then congratulations, you stuck with the same problem as me. It appears with specific projects setting,
estimated flops in particular. If you decrease this parameter for one of the project, it actually can solve
problem, but I don't know the reason and how to eliminate it. If you have thoughts please share them.

A workaround for this problem is to find the line with "Gasp! This exception may be lost by subsequent calls." in your installation of simgrid, comment it and rebuild the library. In the future more normal workaround can be implemented when we first print results and then finish the simulation.

Another problem is described in [this issue](https://github.com/simgrid/simgrid/issues/394). It affects the simulation significantly and isn't fixed yet.

# Explanations of some parts:
1. task flops:
why do we have 

```task->duration = project.job_duration * ((double)req->group_power / req->power); (1)```

and

```(task.msg_task->get_remaining() * client->factor) / power; (2)```

In platform.xml we set up a clients' cluster where hosts have speed of 1Gf. This speed is used for execution tasks 
via SimGrid. Instead of setting different speed to hosts, we adjust amount of computation (1). When we need to calculate
time left for the task, we eliminate this affect by multiplying by client->factor as in (2).

## Epilog
Scheme that helps me to understand what's going on:
[draw.io file](https://drive.google.com/file/d/1AiNDxQ6wiof9eOykej56L1AG8mgznK_Z/view?usp=sharing)
