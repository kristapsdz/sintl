#include <unistd.h>

int
main(void)
{

	return(-1 != pledge("stdio", NULL));
}
