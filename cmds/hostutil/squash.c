

#include <stdio.h>
#include <stdlib.h>	/* Host port: free() */
#include <string.h>	/* Host port: strchr() */
#include <sys/types.h>	/* Host port: ssize_t */

int main()
{
	int copying = 0;
	/* Host port: getline() and the newline removed, as gets() did;	*/
	/* gets() is gone from C11. A line of any length is read whole	*/
	/* (gets() into char line[100] overflowed the stack beyond 99	*/
	/* characters; master.fdb has 83 at most).			*/
	char *line = NULL;
	size_t size = 0;
	while(getline(&line,&size,stdin) != -1)
	{
		char *p = strchr(line,'\n');
		if( p != NULL ) *p = 0;
		p = line;
		if( *p == '#') 
		{
			if( p[1] == '{' ) copying = 1;
			if( p[1] == '}' ) copying = 0;
			continue;
		}
		if( copying ) continue;
		while( *p == '\t' || *p == ' ' || *p == '\r' ) p++;
		if( *p == 0 ) continue;
		printf("%s\n",line);
	}
	free(line);

	return 0;
}
