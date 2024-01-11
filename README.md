# combos

The main difference from the original repository is that the code which simulates BOINC was switched from __boinc_simulator.c__ to __boinc.cpp__. The reason: SimGrid's newest version is 3.34, when combos compiles only with 3.11. I've tried several versions as well and I've tried to play with combos + 3.11v by changing __parameters.xml__. However, it either didn't compile or failed with a segmentation fault. So, the first attempt was only to switch the file's extension and fix all compilation errors.

Besides aforementioned reasons, the code in c requires a careful work with pointers. c++ provides a little bit more guarantees. At least it will shout.

Apparently, the new code generates way too different results than the original version if I run them with the original __parameters.xml__. It can be that I didn't understand how the original code or xbt_structure work in some places. However, I've found several suspicious places in the old code, and I haven't checked them yet (i.e. haven't fixed them in the old version and compared results).

# 'Install' section

I believe it won't work as it because my files are specific for my host. I can say, that I've installed boost and simgrid. Then compile the project in __build/__ via *cmake*.

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

# Work yet to be done
- I close eyes on freeing resources. They might leak a lot.
- I've run valgrind many times to fix UB. ~~The last time the output wasn't clean yet, so fixes yet to be done.~~ The last time, it was clean (except leaks). Still, it's worth checking more. 
- ~~I'm not sure about how an asynchronous communication works with a synchronous one in this project. I would like to spend time to understand it better.~~
- ~~there is a piece of code ```workunit->times[reply->result_number]``` in the original version. In my current understanding, a size of ```workunit->times``` correlates to ```results```, when ```reply->result_number``` is proportional to ```tasks```, so I've got ```out_of_range exception```.~~
- ~~my intrusive lists are structure with ```task-s``` as members. Probably the correct way is to keep pointers ```task_t-s```.~~


## Epilog
Scheme that helps me to understand what's going on:
[draw.io file](https://drive.google.com/file/d/1AiNDxQ6wiof9eOykej56L1AG8mgznK_Z/view?usp=sharing)
