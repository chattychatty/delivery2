#include <omp.h>
int main(){ omp_sched_t s=omp_sched_static; omp_set_schedule(s,1); omp_get_schedule(&s,&(int&)*(new int)); return 0; }
