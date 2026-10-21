# Writes the files INPUTS lists, one after another (a line break between),
# as the bytes of a C++ initializer list, to OUTPUT.
set(content "")
foreach(input ${INPUTS})
    file(READ "${input}" text HEX)
    string(APPEND content "${text}0a")
endforeach()
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," content "${content}")
string(REGEX REPLACE "((0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,))" "\\1\n" content "${content}")
file(WRITE "${OUTPUT}" "${content}")
