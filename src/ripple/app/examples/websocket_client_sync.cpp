#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/lexical_cast.hpp>
#include <string>
#include <cstdlib>
#include <iostream>
#include <string>
using std::string;

namespace ip = boost::asio::ip;
using tcp = boost::asio::ip::tcp;               // from <boost/asio/ip/tcp.hpp>
namespace websocket = boost::beast::websocket;  // from <boost/beast/websocket.hpp>

												// Sends a WebSocket message and prints the response
int websocket_main(const char *shost, int nport, const  char *data)
{
	try
	{
		// Check command line arguments.
		auto const host = shost;
		auto const port = nport;
		auto const text = data;

		// The io_context is required for all I/O
		boost::asio::io_context ioc;

		// These objects perform our I/O
		tcp::resolver resolver{ ioc };
		websocket::stream<tcp::socket> ws{ ioc };

		// Look up the domain name
		tcp::resolver::query query(shost, boost::lexical_cast<string>(nport));

		auto const results = resolver.resolve(query);

		// Make the connection on the IP address we get from a lookup
		boost::asio::connect(ws.next_layer(), results.begin(), results.end());

		// Perform the websocket handshake
		ws.handshake(host, "/");

		// Send the message
		ws.write(boost::asio::buffer(std::string(text)));

		// This buffer will hold the incoming message
		boost::beast::multi_buffer buffer;

		// Read a message into our buffer
		ws.read(buffer);

		// Close the WebSocket connection
		ws.close(websocket::close_code::normal);

		// If we get here then the connection is closed gracefully

		// The buffers() function helps print a ConstBufferSequence
		std::cout << boost::beast::buffers(buffer.data()) << std::endl;
	}
	catch (std::exception const& e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}