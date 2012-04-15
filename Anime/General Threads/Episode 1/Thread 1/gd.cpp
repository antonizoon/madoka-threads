/* This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details.
 *
 * gd - Gallery Downloader
 *
 * Build instructions:
 *   Win32/NT:
 *     Should build without modification
 *     in a modern Visual Studio environment
 *   GNU/Linux & Mac OS X/Darwin & FreeBSD:
 *     g++ gd.cpp -o gd
 *   Haiku:
 *     g++ gd.cpp -o gd -lnetwork
 */

#include <iostream>
#include <string>
#include <string.h>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <cctype>
#include <time.h>

#if defined(_WIN32)
#	define gdOsWindows
#	pragma comment(lib, "ws2_32")
#	include <WinSock2.h>
typedef int socklen_t;
#elif defined(__linux__) || defined(__MACH__) || defined(__HAIKU__) || defined(__FreeBSD__)
#	define gdOsUnixLike
#	include <sys/types.h>
#	include <unistd.h>
#	include <netdb.h>
#	include <arpa/inet.h>
#	include <netinet/in.h>
#	include <fcntl.h>
#	include <sys/time.h>
#	include <sys/socket.h>
#endif

#define gdBufferSize	512
#define gdSocketTimeout	5
#define gdNumRetries	3
#define	gdVersion		"v0.9"

using namespace std;

void gdInitNetwork() {
#ifdef gdOsWindows
	WSADATA wsd;
	if (WSAStartup(MAKEWORD(2,2), &wsd) != 0) {
		cerr << "WinSock failed to initialize!" << endl;
		exit(1);
	}
#endif
}

void gdCleanupNetwork() {
#ifdef gdOsWindows
	WSACleanup();
#endif
}

void gdSetSocketNonblocking(int sock) {
#if defined(gdOsWindows)
	u_long iMode = 1;
	ioctlsocket(sock, FIONBIO, &iMode);
#elif defined(gdOsUnixLike)
	fcntl(sock, F_SETFL, O_NONBLOCK);
#endif
}

void gdCloseSocket(int sock) {
	shutdown(sock, 2);
#if defined(gdOsWindows)
	closesocket(sock);
#elif defined(gdOsUnixLike)
	close(sock);
#endif
}

void gdSleep(int milliseconds) {
#if defined(gdOsWindows)
	Sleep(milliseconds);
#elif defined(gdOsUnixLike)
	usleep(milliseconds * 1000);
#endif
}

string gdToUpperCase(string & str) {
	string newstr = str;
	for (unsigned int i = 0; i < newstr.size(); i++)
		if (newstr[i] >= 'a' && newstr[i] <= 'z')
			newstr[i] -= 32;
	return newstr;
}

string gdToLowerCase(string & str) {
	string newstr = str;
	for (unsigned int i = 0; i < newstr.size(); i++)
		if (newstr[i] >= 'A' && newstr[i] <= 'Z')
			newstr[i] += 32;
	return newstr;
}

void gdShowUsage() {
	cout <<
		"Usage:\n" <<
		" gd [options] <url>\tdownload images from gallery url\n" <<
		"\n" <<
		"Options:\n" <<
		" -d <dir>\t\toutput directory\n" <<
		" -f <\"1,2,3,n\">\t\tfile types to download (default: jpg,jpeg,bmp,png,gif)\n" <<
		" -h, --help\t\tprint this help text\n" <<
		" -o, --overwrite\toverwrite existing files\n" <<
		" -v, --verbose\t\tverbose mode\n" <<
		" -V, --version\t\tprint version info\n" <<
	endl;
	exit(0);
}

bool gdDownload(string url, vector<char> * data, bool verbose) {
	string			website;
	hostent *		host;
	sockaddr_in		hostAddress;
	int				sockfd;
	unsigned int	i;
	string			request;
	vector<char>	reply;
	size_t			offset = (size_t)-1;
	size_t			httpOffset = 0;
	vector<char> &	final = *data;
	final.clear();

	gdInitNetwork();

	// Get host name from hyperlink
	if (0 == memcmp(url.c_str(), "http://", 6))
		httpOffset = 7;
	else if (0 == memcmp(url.c_str(), "https://", 7))
		httpOffset = 8;
	else
		httpOffset = 0;
	for (i = httpOffset; i < url.size(); i++) {
		if (url[i] == '/')
			break;
		website += url[i];
	}

	// Get host address
	if (verbose) cout << "Looking up host name " << website << "..." << endl;
	host = gethostbyname(website.c_str());
	if (!host) {
		cerr << "Host name not found!" << endl;
		return false;
	}
	memset(&hostAddress, 0, sizeof(hostAddress));
	hostAddress.sin_addr.s_addr = *((unsigned long*)host->h_addr_list[0]);

	// Initialize socket
	if (verbose) cout << "Initializing socket..." << endl;
	int			serr = 0;
	fd_set		fds;
	timeval		timeout;
	sockaddr	remote;
	socklen_t	addrlen = sizeof(sockaddr);
	timeout.tv_sec = gdSocketTimeout;
	timeout.tv_usec = 0;
	sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sockfd < 0) {
		cerr << "Socket initialization failed!" << endl;
		return false;
	}
	FD_ZERO(&fds);
	FD_SET(sockfd, &fds);

	// Connect to host
	if (verbose) cout << "Connecting to " << inet_ntoa(hostAddress.sin_addr) << "..." << endl;
	hostAddress.sin_family = AF_INET;
	hostAddress.sin_port = htons(80);
	gdSetSocketNonblocking(sockfd);
	connect(sockfd, (sockaddr*)&hostAddress, sizeof(hostAddress));
	if (1 == select(sockfd+1, 0, &fds, 0, &timeout)) {
		int serr;
		socklen_t len = sizeof(serr);
		getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (char*)&serr, &len);
		if (serr != 0) {
			cerr << "Failed to connect!" << endl;
			gdCloseSocket(sockfd);
			return false;
		}
	} else {
		cerr << "Failed to connect!" << endl;
		gdCloseSocket(sockfd);
		return false;
	}

	// Send GET request
	if (verbose) cout << "Sending http request..." << endl;
	request = string("GET ") + string(&url[httpOffset+strlen(website.c_str())]) +
		string(" HTTP/1.0\r\nHost: ") + website + string("\r\n\r\n");
	if (request.size() != send(sockfd, request.c_str(), request.size(), 0)) {
		cerr << "Couldn't send http request!" << endl;
		gdCloseSocket(sockfd);
		return false;
	}

	// Receive file
	if (verbose) cout << "Receiving..." << endl;
	char		buffer[gdBufferSize];
	bool		done = false;
	int			bytes = 0;
	while (!done) {
		serr = select(sockfd+1, &fds, 0, 0, &timeout);
		if (serr == 0) {
			cout << "Timed out!" << endl;
			gdCloseSocket(sockfd);
			return false;
		} else if (serr < 0) {
			cout << "Socket error!" << endl;
			gdCloseSocket(sockfd);
			return false;
		}
		memset(buffer, 0, gdBufferSize);
		bytes = recvfrom(sockfd, buffer, gdBufferSize, 0, &remote, &addrlen);
		if (bytes < 0) {
			cout << "Socket error: " << bytes << endl;
			gdCloseSocket(sockfd);
			return false;
		} else if (bytes == 0) {
			done = true;
		} else if (bytes > 0) {
			size_t oldsize = reply.size();
			reply.resize(reply.size()+bytes);
			memcpy(&reply[oldsize], buffer, bytes);
		}
	}
	gdCloseSocket(sockfd);

	// Get file offset (end of header)
	for (i = 0; i < reply.size(); i++) {
		if (reply[i] == '\r') {
			if (0 == memcmp(&reply[i+1], "\n\r\n", 3)) {
				offset = i + 4;
				break;
			}
		}
	}

	// Find out if chunk transfer
	bool			isChunk = false;
	const char *	encoding = "Transfer-Encoding: chunked";
	for (i = 0; i < offset; i ++)
		if (reply[i] == encoding[0])
			if (0 == memcmp(&reply[i], encoding, strlen(encoding)))
				isChunk = true;

	// Copy reply with http stuff stripped
	if (isChunk) {
		size_t finalOffset = 0;
		while (true) {
			if (offset >= reply.size())
				break;
			// Read chunk size
			string sizeText;
			size_t sizeNum;
			for (; reply[offset] != '\r' && offset < reply.size(); offset++)
				sizeText += reply[offset];
			offset += 2;
			sizeNum = strtol(sizeText.c_str(), 0, 16);
			if (sizeNum == 0)
				break;
			// Copy chunk to final
			data->resize(final.size() + sizeNum);
			memcpy(&final[finalOffset], &reply[offset], sizeNum);
			finalOffset += sizeNum;
			offset += sizeNum;
			offset += 2;
		}
	} else {
		// Copy everything but the header
		data->resize(reply.size() - offset);
		memcpy(&final[0], &reply[offset], final.size());
	}

	gdCleanupNetwork();
	return true;
}

int main(int argc, char ** argv) {
	string			webPage;
	vector<char>	webPageData;
	string			saveDirectory;
	int				retry;
	bool			verbose = false;
	bool			overwrite = false;
	vector<string>	filetypes;

	// Handle args
	bool gotWebPage = false;
	bool gotFileTypes = false;
	for (int i = 1; i < argc; i++) {
		// Verbose mode
		if (0 == strcmp(argv[i], "-v") || 0 == strcmp(argv[i], "--verbose")) {
			verbose = true;
		// Show version
		} else if (0 == strcmp(argv[i], "-V") || 0 == strcmp(argv[i], "--version")) {
			cout << "Gallery Downloader " << gdVersion << endl;
			exit(0);
		// Save directory
		} else if (0 == strcmp(argv[i], "-d")) {
			if (argc > (i+1)) {
				saveDirectory = argv[i+1];
				// Make sure directory string ends in backslash
				if (saveDirectory[saveDirectory.size()-1] != '/' ||
					saveDirectory[saveDirectory.size()-1] != '\\')
					saveDirectory += '/';
				i++;
			} else
				gdShowUsage();
		// Specify file types to download
		} else if (0 == strcmp(argv[i], "-f")) {
			if (argc > ++i) {
				string fts = argv[i];
				for (unsigned int j = 0; j < fts.size();) {
					string ft;
					bool started = false;
					for (unsigned int k = 0; j < fts.size(); j++) {
						if (!ispunct(fts[j]) && !isspace(fts[j])) {
							started = true;
							ft += fts[j];
						} else {
							if (started)
								break;
							else
								continue;
						}
					}
					if (started) {
						filetypes.push_back(gdToLowerCase(ft));
						filetypes.push_back(gdToUpperCase(ft));
						gotFileTypes = true;
					}

				}
			} else
				gdShowUsage();
		// Overwrite files automatically
		} else if (0 == strcmp(argv[i], "-o") || 0 == strcmp(argv[i], "--overwrite")) {
			overwrite = true;
		// Help
		} else if (0 == strcmp(argv[i], "-h") || 0 == strcmp(argv[i], "--help")) {
			gdShowUsage();
		} else {
			if (!gotWebPage) {
				webPage = argv[i];
				gotWebPage = true;
			} else
				gdShowUsage();
		}
	}
	if (argc < 2 || !gotWebPage)
		gdShowUsage();
	if (!gotFileTypes) {
		filetypes.push_back("jpg");
		filetypes.push_back("jpeg");
		filetypes.push_back("png");
		filetypes.push_back("bmp");
		filetypes.push_back("gif");
		filetypes.push_back("JPG");
		filetypes.push_back("JPEG");
		filetypes.push_back("PNG");
		filetypes.push_back("BMP");
		filetypes.push_back("GIF");
	}

	// Download gallery page
	cout << "Downloading gallery index... ";
	retry = 0;
	if (verbose) cout << endl;
	cout.flush();
	while (true) {
		if (gdDownload(webPage, &webPageData, verbose)) {
			break;
		} else {
			retry++;
			if (retry == gdNumRetries) {
				cerr << "Unable to download!" << endl;
				exit(1);
			}
			cout << "Retrying... ";
		}
	}
	cout << "Done." << endl;

	// Download and save images
	string			imageUrl;
	vector<string>	imageUrls;
	fstream			fout;
	vector<char>	imageData;
	string			imageName;
	for (unsigned int i = 0; i < webPageData.size(); i++) {
		if (webPageData[i] == '\"') {
			// Get image URL
			i++;
			imageUrl.clear();
			while (webPageData[i] != '\"' && i < webPageData.size()) {
				imageUrl += webPageData[i];
				i++;
			}
			// Should we download this Url?
			bool shouldDownload = false;
			for (unsigned int j = 0; j < filetypes.size(); j++) {
				if (imageUrl.size() > filetypes[j].size()) {
					if (0 == memcmp(&imageUrl[imageUrl.size()-filetypes[j].size()],
						filetypes[j].c_str(), filetypes[j].size())) {
						shouldDownload = true;
						break;
					}
				}
			}
			if (!shouldDownload)
				continue;
			// Make sure the it is a full Url, not just a file name
			bool isfullurl = false;
			bool slashstart = false;
			bool longenough = true;
			bool httpstart = false;
			if (imageUrl.size() < 4)
				longenough = false;
			if (longenough)
				httpstart =
					(0 == memcmp(&imageUrl[0], "http", 4) || 0 == memcmp(&imageUrl[0], "HTTP", 4));
			if (httpstart)
				isfullurl = true;
			else if (imageUrl[0] == '/' || imageUrl[0] == '\\')
				slashstart = true;
			// If not, prepend the host name
			if (!isfullurl) {
				if (!slashstart)
					imageUrl = string("/") + imageUrl;
				string oldimageurl = imageUrl;
				imageUrl.clear();
				for (unsigned int j = 0; j < webPage.size(); j++) {
					if (webPage[j] == '/') {
						if (j < (webPage.size()-1)) {
							if (webPage[j+1] == '/') {
								imageUrl += webPage[j++];
								imageUrl += webPage[j];
								continue;
							} else
								break;
						}
					} else
						imageUrl += webPage[j];
				}
				imageUrl += oldimageurl;
			}
			// Have we already downloaded this image?
			bool downloaded = false;
			for (unsigned int j = 0; j < imageUrls.size(); j++) {
				if (imageUrls[j] == imageUrl) {
					downloaded = true;
					break;
				}
			}
			if (downloaded) {
				if (verbose) cout << imageUrl << " already downloaded. Skipping..." << endl;
				continue;
			} else
				imageUrls.push_back(imageUrl);
			// Get image file name
			imageName.clear();
			for (unsigned int j = 0; j < imageUrl.size(); j++) {
				if (imageUrl[j] == '/') {
					imageName.clear();
					continue;
				}
				imageName += imageUrl[j];
			}
			// Does the file already exist?
			if (!overwrite) {
				fstream f((saveDirectory + imageName).c_str(), ios::in);
				if (f.is_open()) {
					cout << imageName << " already exists! Skipping..." << endl;
					continue;
				}
			}
			// Download image
			cout << "Downloading " << imageUrl << "... ";
			retry = 0;
			if (verbose) cout << endl;
			cout.flush();
			while (true) {
				if (gdDownload(imageUrl, &imageData, verbose))
					break;
				else {
					retry++;
					if (retry == gdNumRetries) {
						cout << "Unable to download!" << endl;
						break;
					}
					cout << "Retrying... ";
				}
			}
			if (imageData.size() == 0)
				continue;
			cout << "Done." << endl;
			// Save image to disk
			fout.open((saveDirectory + imageName).c_str(),
				ios::trunc | ios::binary | ios::out);
			if (!fout.good()) {
				cerr << "Couldn't create " << imageName << "!" << endl;
				exit(1);
			}
			fout.write(&imageData[0], imageData.size());
			if (!fout.good()) {
				cerr << "I/O error occurred!" << endl;
				cout.flush();
				fout.close();
				exit(1);
			}
			fout.flush();
			fout.close();
			gdSleep(1);
		}
	}
	if (imageUrls.size() == 0)
		cout << "No images found in gallery!" << endl;
	else
		cout << "Finished!" << endl;

	return 0;
}