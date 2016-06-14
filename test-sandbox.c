#include <sandbox.h>

int
main(void)
{
	char	*ep;
	int	 rc;

	rc = sandbox_init(kSBXProfileNoNetwork, SANDBOX_NAMED, &ep);
	if (0 == rc)
		return(0);
	sandbox_free_error(ep);
	return(1);
}
