#include "kernel/proc.h"
#include "kernel/types.h"

void
threadfunc()
{
  printf("executing...");
  printf("done executing.");
}


int
main(int argc, char *argv[])
{
    create_thread(*threadfunc,NULL);
    create_thread(*threadfunc,NULL);

}