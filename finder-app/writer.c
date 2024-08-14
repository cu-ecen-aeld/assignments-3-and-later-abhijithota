#include <stdio.h>
#include <syslog.h>

int main(int argc, char *argv[])
{
    openlog(NULL, 0, LOG_USER);

    if (argc != 3)
    {
        if (argc < 3)
        {
            syslog(LOG_ERR, "Please provide 2 arguments for filename and string to be written");
        }
        else if (argc > 3)
        {
            syslog(LOG_ERR, "Please provide only 2 arguments, for filename and string to be written");
        }
        closelog();
        return 1;
    }

    FILE *fptr = fopen(argv[1], "w");
    if (fptr == NULL)
    {
        syslog(LOG_ERR, "Error opening file");
        closelog();
        return 1;
    }

    syslog(LOG_DEBUG, "Writing %s to %s", argv[2], argv[1]);
    fputs(argv[2], fptr);

    if(fclose(fptr) !=0)
    {
        syslog(LOG_ERR, "Error closing file");
        closelog();
        return 1;
    }

    closelog();
    return 0;
}