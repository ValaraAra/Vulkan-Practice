#include "application.h"

int main()
{
	Application app;

	if (!app.initialize())
	{
		app.shutdown();
		return 1;
	}

	app.run();
	app.shutdown();

	return 0;
}