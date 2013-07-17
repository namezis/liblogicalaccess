/**
 * \file gunneboreaderunit.cpp
 * \author Maxime C. <maxime-dev@islog.com>
 * \brief Gunnebo reader unit.
 */

#include "gunneboreaderunit.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>

#include "gunneboreaderprovider.hpp"
#include "logicalaccess/services/accesscontrol/cardsformatcomposite.hpp"
#include "logicalaccess/cards/chip.hpp"
#include "readercardadapters/gunneboreadercardadapter.hpp"
#include <boost/filesystem.hpp>
#include "logicalaccess/dynlibrary/librarymanager.hpp"
#include "logicalaccess/dynlibrary/idynlibrary.hpp"

namespace logicalaccess
{
	/*
	 *
	 *  Gunnebo reader only send ID through Serial port. It doesn't support any request command !
	 *
	 */

	GunneboReaderUnit::GunneboReaderUnit(boost::shared_ptr<SerialPortXml> port)
		: ReaderUnit()
	{
		d_readerUnitConfig.reset(new GunneboReaderUnitConfiguration());
		setDefaultReaderCardAdapter (boost::shared_ptr<GunneboReaderCardAdapter> (new GunneboReaderCardAdapter()));
		d_port = port;
		d_card_type = "UNKNOWN";

		try
		{
			boost::property_tree::ptree pt;
			read_xml((boost::filesystem::current_path().string() + "/GunneboReaderUnit.config"), pt);
			d_card_type = pt.get("config.cardType", "UNKNOWN");
		}
		catch (...) { }

		if (!d_port)
		{
			d_port.reset(new SerialPortXml(""));
		}
		d_isAutoDetected = false;
		removalIdentifier.clear();
	}

	GunneboReaderUnit::~GunneboReaderUnit()
	{
		disconnectFromReader();
	}

	boost::shared_ptr<SerialPortXml> GunneboReaderUnit::getSerialPort()
	{
		return d_port;
	}

	void GunneboReaderUnit::setSerialPort(boost::shared_ptr<SerialPortXml> port)
	{
		if (port)
		{
			INFO_("Setting serial port {%s}...", port->getSerialPort()->deviceName().c_str());
			d_port = port;
		}
	}

	std::string GunneboReaderUnit::getName() const
	{
		string ret;
		if (d_port && !d_isAutoDetected)
		{
			ret = d_port->getSerialPort()->deviceName();
		}
		return ret;
	}

	std::string GunneboReaderUnit::getConnectedName()
	{
		string ret;
		if (d_port)
		{
			ret = d_port->getSerialPort()->deviceName();
		}
		return ret;
	}

	void GunneboReaderUnit::setCardType(std::string cardType)
	{
		INFO_("Setting card type {0x%s(%s)}", cardType.c_str(), cardType.c_str());
		d_card_type = cardType;
	}

	bool GunneboReaderUnit::waitInsertion(unsigned int maxwait)
	{
		bool oldValue = Settings::getInstance().IsLogEnabled;
		if (oldValue && !Settings::getInstance().SeeWaitInsertionLog)
		{
			Settings::getInstance().IsLogEnabled = false;		// Disable logs for this part (otherwise too much log output in file)
		}

		INFO_("Waiting insertion... max wait {%u}", maxwait);

		bool inserted = false;
		unsigned int currentWait = 0;
		std::vector<unsigned char> createChipId;
		
		try
		{
			// Gunnebo reader doesn't handle commands but we want to simulate the same behavior that for all readers
			// So we send a dummy commmand which does nothing
			do
			{
				try
				{
					if (removalIdentifier.size() > 0)
					{
						createChipId = removalIdentifier;
					}
					else
					{
						std::vector<unsigned char> cmd;
						cmd.push_back(0xff);	// trick
					
						std::vector<unsigned char> tmpASCIIId = getDefaultGunneboReaderCardAdapter()->sendCommand(cmd);

						if (tmpASCIIId.size() > 0)
						{
							createChipId = processCardId(tmpASCIIId);
						}
					}

					if (createChipId.size() > 0)
					{
						d_insertedChip = ReaderUnit::createChip((d_card_type == "UNKNOWN" ? "GenericTag" : d_card_type), createChipId);
						INFO_SIMPLE_("Chip detected !");
						inserted = true;
					}
				}
				catch (std::exception&)
				{
					// No response received is ignored !
				}

				if (!inserted)
				{
#ifdef _WINDOWS
					Sleep(500);
#elif defined(LINUX)
					usleep(500000);
#endif
					currentWait += 500;
				}
			} while (!inserted && (maxwait == 0 || currentWait < maxwait));
		}
		catch(...)
		{
			Settings::getInstance().IsLogEnabled = oldValue;
			throw;
		}

		removalIdentifier.clear();

		INFO_("Returns card inserted ? {%d} function timeout expired ? {%d}", inserted, (maxwait != 0 && currentWait >= maxwait));
		Settings::getInstance().IsLogEnabled = oldValue;

		return inserted;
	}

	bool GunneboReaderUnit::waitRemoval(unsigned int maxwait)
	{
		bool oldValue = Settings::getInstance().IsLogEnabled;
		if (oldValue && !Settings::getInstance().SeeWaitRemovalLog)
		{
			Settings::getInstance().IsLogEnabled = false;		// Disable logs for this part (otherwise too much log output in file)
		}

		INFO_("Waiting removal... max wait {%u}", maxwait);
		bool removed = false;
		unsigned int currentWait = 0;
		removalIdentifier.clear();

		try
		{
			// The inserted chip will stay inserted until a new identifier is read on the serial port.
			if (d_insertedChip)
			{
				do
				{
					try
					{
						std::vector<unsigned char> cmd;
						cmd.push_back(0xff);	// trick

						std::vector<unsigned char> buf = getDefaultGunneboReaderCardAdapter()->sendCommand(cmd);
						if (buf.size() > 0)
						{
							std::vector<unsigned char> tmpId = processCardId(buf);
							if (tmpId.size() > 0 && (tmpId != d_insertedChip->getChipIdentifier()))
							{
								INFO_SIMPLE_("Card found but not same chip ! The previous card has been removed !");
								d_insertedChip.reset();
								removalIdentifier = tmpId;
								removed = true;
							}
						}
					}
					catch (std::exception&)
					{
						// No response received is ignored !
					}

					if (!removed)
					{
#ifdef _WINDOWS
						Sleep(500);
#elif defined(LINUX)
						usleep(500000);
#endif
						currentWait += 500;
					}
				} while (!removed && (maxwait == 0 || currentWait < maxwait));
			}
		}
		catch(...)
		{
			Settings::getInstance().IsLogEnabled = oldValue;
			throw;
		}

		INFO_("Returns card removed ? {%d} - function timeout expired ? {%d}", removed, (maxwait != 0 && currentWait >= maxwait));

		Settings::getInstance().IsLogEnabled = oldValue;

		return removed;
	}

	std::vector<unsigned char> GunneboReaderUnit::processCardId(std::vector<unsigned char>& rawSerialData)
	{
		std::vector<unsigned char> ret;

		if (rawSerialData.size() > 0)
		{
			unsigned long long l = atoull(BufferHelper::getStdString(rawSerialData));
			char bufTmpId[128];
			memset(bufTmpId, 0x00, sizeof(bufTmpId));
#if !defined(__unix__)
			sprintf_s(bufTmpId, sizeof(bufTmpId), "%012llx", l);
#else
			sprintf(bufTmpId, "%012llx", l);
#endif

			ret = formatHexString(std::string(bufTmpId));
		}

		return ret;
	}

	bool GunneboReaderUnit::connect()
	{
		WARNING_SIMPLE_("Connect do nothing with Gunnebo reader");
		return true;
	}

	void GunneboReaderUnit::disconnect()
	{
		WARNING_SIMPLE_("Disconnect do nothing with Gunnebo reader");
	}
	
	boost::shared_ptr<Chip> GunneboReaderUnit::createChip(std::string type)
	{
		INFO_("Creating chip... chip type {0x%s(%s)}", type.c_str());
		boost::shared_ptr<Chip> chip = ReaderUnit::createChip(type);

		if (chip)
		{
			INFO_SIMPLE_("Chip created successfully !");
			boost::shared_ptr<ReaderCardAdapter> rca;
			boost::shared_ptr<CardProvider> cp;

			if (type == "GenericTag")
			{
				INFO_SIMPLE_("Generic tag Chip created");
				rca = getDefaultReaderCardAdapter();
				cp = LibraryManager::getInstance()->getCardProvider("GenericTag");
			}
			else
				return chip;

			rca->setReaderUnit(shared_from_this());
			chip->setCardProvider(cp);
		}
		return chip;
	}

	boost::shared_ptr<Chip> GunneboReaderUnit::getSingleChip()
	{
		boost::shared_ptr<Chip> chip = d_insertedChip;
		return chip;
	}

	std::vector<boost::shared_ptr<Chip> > GunneboReaderUnit::getChipList()
	{
		std::vector<boost::shared_ptr<Chip> > chipList;
		boost::shared_ptr<Chip> singleChip = getSingleChip();
		if (singleChip)
		{
			chipList.push_back(singleChip);
		}
		return chipList;
	}

	boost::shared_ptr<GunneboReaderCardAdapter> GunneboReaderUnit::getDefaultGunneboReaderCardAdapter()
	{
		boost::shared_ptr<ReaderCardAdapter> adapter = getDefaultReaderCardAdapter();
		return boost::dynamic_pointer_cast<GunneboReaderCardAdapter>(adapter);
	}

	string GunneboReaderUnit::getReaderSerialNumber()
	{
		WARNING_SIMPLE_("Do nothing with Gunnebo reader");
		string ret;
		return ret;
	}

	bool GunneboReaderUnit::isConnected()
	{
		if (d_insertedChip)
			INFO_SIMPLE_("Is connected {1}");
		else
			INFO_SIMPLE_("Is connected {0}");
		return bool(d_insertedChip);
	}

	bool GunneboReaderUnit::connectToReader()
	{
		INFO_SIMPLE_("Connecting to reader...");
		bool ret = false;

		startAutoDetect();

		EXCEPTION_ASSERT_WITH_LOG(getSerialPort(), LibLogicalAccessException, "No serial port configured !");
		EXCEPTION_ASSERT_WITH_LOG(getSerialPort()->getSerialPort()->deviceName() != "", LibLogicalAccessException, "Serial port name is empty ! Auto-detect failed !");

		if (!getSerialPort()->getSerialPort()->isOpen())
		{
			INFO_SIMPLE_("Serial port closed ! Opening it...");
			getSerialPort()->getSerialPort()->open();
			configure();
			ret = true;
		}
		else
		{
			INFO_SIMPLE_("Serial port already opened !");
			ret = true;
		}

		return ret;
	}

	void GunneboReaderUnit::disconnectFromReader()
	{
		INFO_SIMPLE_("Disconnecting from reader...");
		if (getSerialPort()->getSerialPort()->isOpen())
		{
			getSerialPort()->getSerialPort()->close();
		}
	}

	void GunneboReaderUnit::startAutoDetect()
	{
		if (d_port && d_port->getSerialPort()->deviceName() == "")
		{
			if (!Settings::getInstance().IsAutoDetectEnabled)
			{
				INFO_SIMPLE_("Auto detection is disabled through settings !");
				return;
			}

			INFO_SIMPLE_("Serial port is empty ! Starting Auto COM Port Detection...");
			std::vector<boost::shared_ptr<SerialPortXml> > ports;
			if (SerialPortXml::EnumerateUsingCreateFile(ports) && !ports.empty())
			{
				bool found = false;
				for (std::vector<boost::shared_ptr<SerialPortXml> >::iterator i  = ports.begin(); i != ports.end() && !found; ++i)
				{
					try
					{
						INFO_("Processing port {%s}...", (*i)->getSerialPort()->deviceName().c_str());
						(*i)->getSerialPort()->open();
						configure((*i), false);

						boost::shared_ptr<GunneboReaderUnit> testingReaderUnit(new GunneboReaderUnit(*i));
						boost::shared_ptr<GunneboReaderCardAdapter> testingCardAdapter(new GunneboReaderCardAdapter());
						testingCardAdapter->setReaderUnit(testingReaderUnit);
						
						std::vector<unsigned char> cmd;
						cmd.push_back(0xff);	// trick

						std::vector<unsigned char> tmpASCIIId = testingCardAdapter->sendCommand(cmd, Settings::getInstance().AutoDetectionTimeout);

						INFO_SIMPLE_("Reader found ! Using this COM port !");
						d_port = (*i);
						found = true;
					}
					catch (LibLogicalAccessException& e)
					{
						ERROR_("Exception {%s}", e.what());
					}
					catch (...)
					{
						ERROR_SIMPLE_("Exception received !");
					}

					if ((*i)->getSerialPort()->isOpen())
					{
						(*i)->getSerialPort()->close();
					}
				}

				if (!found)
				{
					INFO_SIMPLE_("NO Reader found on COM port...");
				}
				else
				{
					d_isAutoDetected = true;
				}
			}
			else
			{
				WARNING_SIMPLE_("No COM Port detected !");
			}
		}
	}

	void GunneboReaderUnit::configure()
	{
		configure(getSerialPort(), Settings::getInstance().IsConfigurationRetryEnabled);
	}

	void GunneboReaderUnit::configure(boost::shared_ptr<SerialPortXml> port, bool retryConfiguring)
	{
		EXCEPTION_ASSERT_WITH_LOG(port, LibLogicalAccessException, "No serial port configured !");
		EXCEPTION_ASSERT_WITH_LOG(port->getSerialPort()->deviceName() != "", LibLogicalAccessException, "Serial port name is empty ! Auto-detect failed !");

		try
		{
#ifndef _WINDOWS
			struct termios options = port->getSerialPort()->configuration();

			/* Set speed */
			cfsetispeed(&options, B9600);
			cfsetospeed(&options, B9600);

			/* Enable the receiver and set local mode */
			options.c_cflag |= (CLOCAL | CREAD);

			/* Set character size and parity check */
			/* 8N1 */
			options.c_cflag &= ~PARENB;
			options.c_cflag &= ~CSTOPB;
			options.c_cflag &= ~CSIZE;
			options.c_cflag |= CS8;

			/* Disable parity check and fancy stuff */
			options.c_iflag &= ~ICRNL;
			options.c_iflag &= ~INPCK;
			options.c_iflag &= ~ISTRIP;

			/* Disable software flow control */
			options.c_iflag &= ~(IXON | IXOFF | IXANY);

			/* RAW input */
			options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

			/* RAW output */
			options.c_oflag &= ~OPOST;

			/* Timeouts */
			options.c_cc[VMIN] = 1;
			options.c_cc[VTIME] = 5;

			port->getSerialPort()->setConfiguration(options);
#else
			DCB options = port->getSerialPort()->configuration();
			options.BaudRate = CBR_9600;
			options.fBinary = TRUE;               // Binary mode; no EOF check
			options.fParity = FALSE;               // Enable parity checking
			options.fOutxCtsFlow = FALSE;         // No CTS output flow control
			options.fOutxDsrFlow = FALSE;         // No DSR output flow control
			options.fDtrControl = DTR_CONTROL_DISABLE;
													// DTR flow control type
			options.fDsrSensitivity = FALSE;      // DSR sensitivity
			options.fTXContinueOnXoff = TRUE;     // XOFF continues Tx
			options.fOutX = FALSE;                // No XON/XOFF out flow control
			options.fInX = FALSE;                 // No XON/XOFF in flow control
			options.fErrorChar = FALSE;           // Disable error replacement
			options.fNull = FALSE;                // Disable null stripping
			options.fRtsControl = RTS_CONTROL_DISABLE;
													// RTS flow control
			options.fAbortOnError = FALSE;        // Do not abort reads/writes on
													// error
			options.ByteSize = 8;                 // Number of bits/byte, 4-8
			options.Parity = NOPARITY;            // 0-4=no,odd,even,mark,space
			options.StopBits = ONESTOPBIT;        // 0,1,2 = 1, 1.5, 2
			port->getSerialPort()->setConfiguration(options);
#endif
		}
		catch(std::exception& e)
		{
			if (retryConfiguring)
			{
				// Strange stuff is going here... by waiting and reopening the COM port (maybe for system cleanup), it's working !
				std::string portn = port->getSerialPort()->deviceName();
				WARNING_("Exception received {%s} ! Sleeping {%d} milliseconds -> Reopen serial port {%s} -> Finally retry  to configure...",
							e.what(), Settings::getInstance().ConfigurationRetryTimeout, portn.c_str());
#if !defined(__unix__)
				Sleep(Settings::getInstance().ConfigurationRetryTimeout);
#else
				sleep(Settings::getInstance().ConfigurationRetryTimeout);
#endif
				port->getSerialPort()->reopen();
				configure(getSerialPort(), false);
			}
		}
	}

	void GunneboReaderUnit::serialize(boost::property_tree::ptree& parentNode)
	{
		boost::property_tree::ptree node;
		
		node.put("<xmlattr>.type", getReaderProvider()->getRPType());
		d_port->serialize(node);
		d_readerUnitConfig->serialize(node);

		parentNode.add_child(getDefaultXmlNodeName(), node);
	}

	void GunneboReaderUnit::unSerialize(boost::property_tree::ptree& node)
	{
		d_port.reset(new SerialPortXml());
		d_port->unSerialize(node.get_child(d_port->getDefaultXmlNodeName()));
		d_readerUnitConfig->unSerialize(node.get_child(d_readerUnitConfig->getDefaultXmlNodeName()));
	}

	boost::shared_ptr<GunneboReaderProvider> GunneboReaderUnit::getGunneboReaderProvider() const
	{
		return boost::dynamic_pointer_cast<GunneboReaderProvider>(getReaderProvider());
	}
}
