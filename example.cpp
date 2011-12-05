#include <iostream>

int main(int argc, char** argv) {
	switch(argc) {
		case 1:
			std::cout << "One argument" << std::endl;
			break;
		case 2:
		case 3:
			std::cout << "Two or three arguments" << std::endl;
			break;
		default:
			std::cout << "More than three arguments" << std::endl;
	}

	if (argc > 2)
		return 0;
	
	return 1;
}
