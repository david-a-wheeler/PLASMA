// Digraphs for braces, and comments in odd places.
int one() <% return 1; %>   /* a block comment
                               spanning lines */
int main() <%
  __puti(one() // a line comment inside an expression
       + /* here too */ 2);
  __putln();
  return 0;
%>
