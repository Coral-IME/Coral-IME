#include "pipe_session_client.h"

int main()
{
	auto port = coral::transport::make_pipe_session_client(
		coral::transport::default_session_pipe(), 1000);
	if (!port || !port->available())
		return 2;
	const auto session = port->open_session();
	if (!session)
		return 3;
	port->close_session(session);
	return 0;
}
