/* Replacement for the recovered table builder's direct va_list access. */
static void *keymap_alloc(unsigned int size)
{
    void *p = malloc(size);
    if (!p) FatalError("Cannot allocate keyboard map");
    return p;
}
#define malloc keymap_alloc

static void *copy_array(int element_size, int count, int supplied, const int *values)
{
    int i;
    void *result;
    if (count != supplied) FatalError("Keyboard map element count mismatch");
    result = keymap_alloc(element_size * count);
    for (i = 0; i < count; ++i) {
        if (element_size == sizeof(KeySym)) ((KeySym *)result)[i] = values[i];
        else if (element_size == 1) ((unsigned char *)result)[i] = values[i];
        else FatalError("Invalid keyboard map element size");
    }
    return result;
}
#define create_array(size, count, ...) \
    copy_array(size, count, sizeof((int[]){__VA_ARGS__}) / sizeof(int), (int[]){__VA_ARGS__})
