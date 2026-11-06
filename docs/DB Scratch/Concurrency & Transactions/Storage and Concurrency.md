Currently storage hasnt been concerned with concurrent requests.

Buffer manager is a part of shared memory, across all threads of DB_Pipeline.

Here mostly locks will be considered and not latches.

Most probably a **single buffer lock could be held on whole buffer manager**. (already there)

A single lock could mean contention. Current 259us.

![[Pasted image 20261104235009.png]]

**Buffer Pool Lock**

Lock queue should be implemented over buffer pool.

This queue queues locks, reader/writer, this reduces contention while maintaining consistency of locks.

As lock would be acquired over physical structure (buffer pool), store those in queue.

Take a representative of a client, have 2 separate locks reader_lock, writer_lock.

There are 2 invariant:
1. the order of request should hold.
2. the thread should physically wait, as it is a os lock.

for '1' queue could be used but what are the elements inside queue? for '2' cond var could be used to sleep the os level thread, but then what about '1', as no order is held.

hence a vector of condition variable could be used.

lock:
- when thread calls lock, pushes this cond var to queue and puts itself to sleep on it.
- buffer pool after handling a thread from pool would wake this one up.

unlock:
- once whatever thread wants to do is done, it would call unlock at which point buffer pool would pop that element out and process next one.

*Reader/Writer Lock*: specific thing is that these can be read/write locks, if the front is read and next is read too, then both of them and be waked up and pop (this could become a chain), if next is write then don't.

As we now have a system which needs processing of locks periodically, this is done in daemon as buffer pool is shared memory.

Samples of sched_switch event do not have callchains.
           time    cpu  task name                       wait time  sch delay   run time
                        tid/pid                          (msec)     (msec)     (msec)
--------------- ------  ------------------------------  ---------  ---------  ---------
    6469.644577 [0000]  perf[9221]                          0.000      0.000      0.000
    6469.644586 [0000]  migration/0[20]                     0.000      0.002      0.009
    6469.644645 [0001]  perf[9221]                          0.000      0.000      0.000
    6469.644653 [0001]  migration/1[26]                     0.000      0.002      0.008
    6469.644712 [0002]  perf[9221]                          0.000      0.000      0.000
    6469.644721 [0002]  migration/2[32]                     0.000      0.002      0.008
    6469.644777 [0003]  perf[9221]                          0.000      0.000      0.000
    6469.644785 [0003]  migration/3[38]                     0.000      0.002      0.008
    6469.644844 [0004]  perf[9221]                          0.000      0.000      0.000
    6469.644852 [0004]  migration/4[44]                     0.000      0.002      0.008
    6469.644912 [0005]  perf[9221]                          0.000      0.000      0.000
    6469.644920 [0005]  migration/5[50]                     0.000      0.002      0.008
    6469.644976 [0006]  perf[9221]                          0.000      0.000      0.000
    6469.644983 [0006]  migration/6[56]                     0.000      0.002      0.007
    6469.645102 [0007]  perf[9221]                          0.000      0.000      0.000
    6469.645108 [0001]  <idle>                              0.000      0.000      0.454
    6469.645782 [0007]  <idle>                              0.000      0.000      0.680
    6469.645785 [0007]  rcu_preempt[16]                     0.000      0.003      0.002
    6469.645795 [0006]  <idle>                              0.000      0.000      0.811
    6469.645800 [0006]  kworker/u32:0-e[8077]               0.000      0.055      0.005
    6469.646620 [0006]  <idle>                              0.005      0.005      0.819
    6469.646626 [0006]  kworker/u32:0-e[8077]               0.819      0.038      0.005
    6469.646631 [0005]  <idle>                              0.000      0.000      1.710
    6469.646662 [0005]  sudo[9218]                          0.000      0.007      0.031
    6469.646696 [0006]  <idle>                              0.005      0.005      0.069
    6469.646702 [0006]  kworker/u32:0-e[8077]               0.069      0.041      0.006
    6469.646706 [0002]  <idle>                              0.000      0.000      1.985
    6469.646791 [0007]  <idle>                              0.002      0.002      1.005
    6469.646794 [0007]  rcu_preempt[16]                     1.005      0.052      0.003
    6469.646812 [0006]  <idle>                              0.006      0.006      0.109
    6469.646817 [0006]  kworker/u32:0-e[8077]               0.109      0.004      0.004
    6469.646821 [0005]  <idle>                              0.031      0.031      0.158
    6469.646852 [0000]  <idle>                              0.000      0.000      2.266
    6469.646853 [0006]  <idle>                              0.004      0.004      0.036
    6469.646855 [0004]  <idle>                              0.000      0.000      2.002
    6469.646860 [0005]  io-reader[1093/711]                 0.000      0.006      0.039
    6469.646867 [0006]  buffer_pool_con[9226/9222]          0.000      0.003      0.013
    6469.646867 [0007]  <idle>                              0.003      0.003      0.072
    6469.646872 [0005]  <idle>                              0.039      0.039      0.011
    6469.646877 [0007]  buffer_pool_con[9225/9222]          0.000      0.042      0.010
    6469.646880 [0001]  buffer_pool_con[9222]               0.000      0.042      1.771
    6469.646880 [0005]  buffer_pool_con[9227/9222]          0.000      0.003      0.008
    6469.646892 [0001]  <idle>                              1.771      1.771      0.012
    6469.646919 [0006]  <idle>                              0.013      0.013      0.052
    6469.646923 [0006]  kworker/u32:0-e[8077]               0.102      0.003      0.003
    6469.646926 [0005]  <idle>                              0.008      0.008      0.045
    6469.646935 [0006]  <idle>                              0.003      0.003      0.012
    6469.646939 [0000]  buffer_pool_con[9224/9222]          0.000      0.047      0.086
    6469.646943 [0000]  <idle>                              0.086      0.086      0.004
    6469.646945 [0005]  sudo[9218]                          0.264      0.005      0.019
    6469.646951 [0005]  kworker/u32:0-e[8077]               0.022      0.008      0.005
    6469.646953 [0007]  <idle>                              0.010      0.010      0.075
    6469.646955 [0003]  <idle>                              0.000      0.000      2.170
    6469.646963 [0005]  <idle>                              0.024      0.024      0.011
    6469.646972 [0000]  buffer_pool_con[-1/9222]            0.000      0.000      0.028
    6469.646973 [0006]  buffer_pool_con[-1/9222]            0.000      0.000      0.038
    6469.646976 [0007]  buffer_pool_con[9228/9222]          0.000      0.000      0.022
    6469.646985 [0006]  <idle>                              0.038      0.038      0.011
    6469.646988 [0003]  buffer_pool_con[-1/9222]            0.000      0.000      0.032
    6469.646993 [0001]  buffer_pool_con[9222]               0.012      0.007      0.100
    6469.646994 [0003]  buffer_pool_con[-1/9222]            0.000      0.000      0.006
    6469.646994 [0007]  buffer_pool_con[-1/9222]            0.000      0.000      0.018
    6469.646995 [0005]  buffer_pool_con[-1/9222]            0.000      0.000      0.031
    6469.646997 [0001]  <idle>                              0.100      0.100      0.003
    6469.647003 [0006]  buffer_pool_con[-1/9222]            0.000      0.000      0.018
    6469.647059 [0005]  buffer_pool_con                     0.000      0.000      0.064
    6469.647062 [0005]  <idle>                              0.096      0.096      0.003
    6469.647072 [0005]  buffer_pool_con[-1/9222]            0.058      0.000      0.009
    6469.647081 [0005]  <idle>                              0.009      0.009      0.009
    6469.647086 [0005]  kworker/u32:0-e[8077]               0.130      0.002      0.005
    6469.647088 [0006]  <idle>                              0.018      0.018      0.084
    6469.647102 [0005]  <idle>                              0.005      0.005      0.015
    6469.647105 [0006]  sudo[9218]                          0.142      0.005      0.016
    6469.647105 [0005]  kworker/u32:0-e[8077]               0.015      0.002      0.002
    6469.647124 [0007]  <idle>                              0.041      0.041      0.130
    6469.647128 [0007]  kworker/u32:3-e[8825]               0.000      0.041      0.003
    6469.647149 [0006]  <idle>                              0.016      0.016      0.044
    6469.647167 [0004]  renderer[1076/711]                  0.000      0.023      0.311
    6469.647183 [0007]  <idle>                              0.003      0.003      0.055
    6469.647188 [0007]  kworker/u32:3-e[8825]               0.055      0.039      0.005
    6469.647191 [0005]  <idle>                              0.002      0.002      0.086
    6469.647205 [0005]  sudo[9218]                          0.086      0.006      0.014
    6469.647230 [0001]  buffer_pool_con[9222]               0.000      0.000      0.232
    6469.647246 [0007]  <idle>                              0.005      0.005      0.057
    6469.647250 [0007]  kworker/u32:3fe[8825]               0.057      0.045      0.003
    6469.647265 [0000]  <idle>                              0.028      0.028      0.292
    6469.647313 [0001]  <idle>                              0.232      0.232      0.083
    6469.647496 [0007]  <idle>                              0.003      0.003      0.245
    6469.647500 [0005]  <idle>                              0.014      0.014      0.294
    6469.647501 [0007]  kworker/u32:3-e[8825]               0.245      0.002      0.005
    6469.647524 [0003]  <idle>                              0.038      0.038      0.529
    6469.647525 [0005]  io-reader[1093/711]                 0.639      0.003      0.024
    6469.647537 [0003]  io[1077/711]                        0.000      0.003      0.013
    6469.647545 [0004]  <idle>                              0.311      0.311      0.378
    6469.647548 [0007]  <idle>                              0.005      0.005      0.046
    6469.647551 [0005]  <idle>                              0.024      0.024      0.025
    6469.647554 [0002]  tmux: server[4693]                  0.000      0.006      0.848
    6469.647556 [0007]  kworker/u32:3-e[8825]               0.046      0.002      0.008
    6469.647563 [0005]  io-reader[1093/711]                 0.025      0.001      0.011
    6469.647577 [0003]  <idle>                              0.013      0.013      0.039
    6469.647681 [0004]  renderer[1076/711]                  0.378      0.040      0.135
    6469.647699 [0005]  <idle>                              0.011      0.011      0.136
    6469.647787 [0006]  ghostty[711]                        0.000      0.005      0.638
    6469.647804 [0007]  <idle>                              0.008      0.008      0.2472
