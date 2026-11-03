# Simple tail recursion optimization

ccish isn't Scheme, but I think there's a simple tail recursion optimization we can do.

Given:

~~~
int foo(...) {
  ...
    return foo(...);
  ...
}

This would normally be generated as something like:

~~~
ENTER
...
CALL FOO
...
~~~

However, I think in this case the "CALL FOO" can be optimized
into an unconditional branch/jump immediately after the ENTER
(or at the beginning of the function if there is no ENTER).

We can probably unconditionally add a label to the beginning of a function
after its ENTER (or at its real beginning, if there is no ENTER).
Then jump to it, instead of calling.

This would mean that simple tail recusions wouldn't increase stack space use
(a good thing since the stack is limited) and would optimize each
ENTER/LEAVE call.

Note that this doesn't try to do a general tail recursion optimization;
that's harder.

Please see if this is practical to implement, and if so, do so.
