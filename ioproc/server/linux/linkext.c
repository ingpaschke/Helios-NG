/* Default for Linux builds without an external link backend. */
int ios_linux_extra_link(const char *name)
{
    (void)name;
    return 0;
}
