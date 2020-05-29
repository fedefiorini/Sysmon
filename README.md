Sysmon
===========================================
by [Lei Liu](http://www.escience.cn/people/LiuLei2010ict/index.html), Hao Yang, Mengyao Xie, Yong Li, Mingjie Xing

Changes and updates by [Federico Fiorini](https://www.linkedin.com/in/fedefiorini13/).
The current changes make this monitoring tool working with newer versions of the Linux Kernel, as it was built around 4.15.0 (Ubuntu 18.04 LTS).

Release Overview
----------------

This new version of the module should give more comprehensive statistics concerning the memory access patterns and pages hotness in the system, allowing an online profiling and monitoring.
The result of the profiling will then be feeded to an heuristics that determines the correct data placement, both online and offline.

This will help using RAM more efficiently, and allow exploitation of hybrid memory architectures.

Disclaimer
----------

This work is part of my Master's Thesis in Embedded Systems at the Delft University of Technology. 
