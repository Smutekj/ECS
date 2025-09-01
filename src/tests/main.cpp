#include "test.cc"

//! 
REGISTER(CompA)
REGISTER(CompB)
REGISTER(CompC)
REGISTER(CompD)
REGISTER(CompFunction)
REGISTER(CompSharedPtr)





int main(int argc, char **argv)
{ 
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}