// expect: :4: error 219: 'extern' in a block is not supported: declare it at file scope
int main()
{
  for (extern int i; ; )
    return 0;
  return 1;
}
