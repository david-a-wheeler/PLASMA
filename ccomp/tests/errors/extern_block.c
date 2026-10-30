// expect: :4: error 219: 'extern' in a block is not supported: declare it at file scope
int main()
{
  extern int x;
  return 0;
}
