#include <vox/engine.hpp>

#include <iostream>
#include <exception>


int main(int argc, char** argv)
{
	engine::VoxEngine voxEngine{};
	try
	{
		voxEngine.init(nullptr, "Assets", argc > 1 ? argv[1] : "");

		while (voxEngine.update()) {}

		voxEngine.shutdown();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Engine error: " << e.what() << std::endl;
		return 1;
	}
	catch (...)
	{
		std::cerr << "Unspecified engine error has occurred." << std::endl;
		return 1;
	}

	return 0;
}
