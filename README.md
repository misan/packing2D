Through the magic of Google's Gemini Pro 2.5 I have got this library https://github.com/mses-bly/2D-Bin-Packing migrated into C++ so it can be used from Python too. 

Both the code and the .md files (except this README.md) have been created by the IA and not by me, though I would say I helped in the process ;-)

I have tested this code on an M1 Mac and in Ubuntu Linux. 

Some of the features, such as parallel execution, ended up working more slowly than the sequential one, though in some cases, they manage to create a better packing. Just know it is not any faster.

*Code depends on pybind11, Boost, TBB, and Google Test libraries*.
