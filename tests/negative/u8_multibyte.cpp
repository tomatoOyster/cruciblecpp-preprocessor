// C++20 u8 character constant has type char (one code unit).
#if u8'中'
#error multi-byte u8 character unexpectedly accepted
#endif
