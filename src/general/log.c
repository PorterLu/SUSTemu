#include <log.h>

char *log_file = NULL;
FILE *log_fp = NULL;

void init_log(char *log_file)
{
	log_fp = NULL;   /* default: log disabled */
	if(log_file != NULL)
	{
		FILE *fp = fopen(log_file, "w+");
		Assert(fp, "Can note open '%s'", log_file);
		log_fp = fp;
	}
}
