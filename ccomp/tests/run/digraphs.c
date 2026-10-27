// Digraphs for braces, and comments in odd places.
int one() <% return 1; %>   /* a block comment
                               spanning lines */
int main() <%
    puti(one() // a line comment inside an expression
         + /* here too */ 2);
    putln();
    return 0;
%>
