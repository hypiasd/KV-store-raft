#include <iostream>

auto func(std::string flag)
{
    if (flag == "")
        return 2;
    else if (flag == "int")
        return 1;
    else if (flag == "double")
        return 3.14;
}