#include "specreaderimpl.h"
#include "specreader.h"
#include "../apngframe.h"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/regex.hpp>
#include <boost/range/algorithm.hpp>

namespace apngasm {
namespace specreader {
  namespace {
    // Convert string to unsigned int.
    unsigned int s2u(const std::string& str, const unsigned int defaultValue)
    {
      try
      {
        return boost::lexical_cast<unsigned int>(str);
      }
      catch(boost::bad_lexical_cast& e)
      {
        return defaultValue;
      }
    }

    // Convert string to delay parameter.
    Delay str2delay(const std::string& str)
    {
      static const char delimiter = '/';

      Delay delay;
      const std::string::size_type index = str.find(delimiter, 0);

      // Numerator only.
      if(index == std::string::npos)
      {
        delay.num = s2u(str, DEFAULT_FRAME_NUMERATOR);
        delay.den = DEFAULT_FRAME_DENOMINATOR;
      }

      // Numerator / Denominator
      else
      {
        const std::string& num = str.substr(0, index);
        const std::string& den = str.substr(index+1, str.length());

        delay.num = s2u(num, DEFAULT_FRAME_NUMERATOR);
        delay.den = s2u(den, DEFAULT_FRAME_DENOMINATOR);
      }

      return delay;
    }

    // Get file path vector.
    const std::vector<std::string>& getFiles(const std::string& filepath)
    {
      static std::vector<std::string> files;

      boost::filesystem::path absPath( boost::filesystem::absolute(filepath) );

      // Clear temporary vector.
      files.clear();

      // File is unique.
      if( absPath.string().find('*', 0) == std::string::npos )
      {
        if( boost::algorithm::iends_with(absPath.string(), ".png") )
          files.push_back(absPath.string());
        else
          files.push_back(absPath.string() + ".png");
      }

      // File path has wildcard.
      else
      {
        // Convert filepath.
        static const boost::regex escape("[\\^\\.\\$\\|\\(\\)\\[\\]\\+\\?\\\\]");
        static const boost::regex wildcard("\\*");

        absPath = boost::regex_replace(absPath.string(), escape, "\\\\$0");
        absPath = boost::regex_replace(absPath.string(), wildcard, ".+");

        // Skip if directory is not found.
        if( !boost::filesystem::exists(absPath.parent_path()) )
          return files;

        // Search files.
        const boost::regex filter(absPath.string());
        const boost::filesystem::directory_iterator itEnd;
        for(boost::filesystem::directory_iterator itCur(absPath.parent_path());  itCur != itEnd;  ++itCur)
        {
          // Skip if not a file.
          if( !boost::filesystem::is_regular_file(itCur->status()) )
            continue;

          // Skip if no match.
          const std::string& curFilePath = itCur->path().string();
          if( !boost::regex_match(curFilePath, filter) )
            continue;

          // Add filepath if extension is png.
          if( boost::algorithm::iends_with(curFilePath, ".png") )
            files.push_back(curFilePath);
        }

        // Sort vector.
        boost::sort(files);
      }

      return files;
    }
  } // unnamed namespace

  // Initialize SpecReader object.
  SpecReader::SpecReader(const std::string& filePath)
  {
    // File is JSON.
    if( boost::algorithm::iends_with(filePath, ".json") )
      _pImpl = new JsonSpecReader(filePath);
    // File is XML.
    else
      _pImpl = new XmlSpecReader(filePath);
  }

  // Finalize SpecReader object.
  SpecReader::~SpecReader()
  {
    if(_pImpl)
    {
      delete _pImpl;
      _pImpl = NULL;
    }
  }

  // Return animation name.
  const std::string& SpecReader::getName() const
  {
    return _pImpl->getName();
  }

  // Return loops.
  unsigned int SpecReader::getLoops() const
  {
    return _pImpl->getLoops();
  }

  // Return flag of skip first.
  bool SpecReader::getSkipFirst() const
  {
    return _pImpl->getSkipFirst();
  }

  // Return frame information vector.
  const std::vector<FrameInfo>& SpecReader::getFrameInfos() const
  {
    return _pImpl->getFrameInfos();
  }

  // Initialize AbstractSpecReader object.
  AbstractSpecReader::AbstractSpecReader()
    : _name("")
    , _loops(0)
    , _skipFirst(false)
    , _frameInfos()
  {
    // nop
  }
  
  // Initialize JsonSpecReader object.
  JsonSpecReader::JsonSpecReader(const std::string& filePath)
  {
    // Read JSON file.
    boost::property_tree::ptree root;
    boost::property_tree::read_json(filePath, root);

    // Set current directory.
    const boost::filesystem::path oldPath = boost::filesystem::current_path();
    boost::filesystem::current_path( boost::filesystem::path(filePath).parent_path() );

    // name
    if( boost::optional<std::string> name = root.get_optional<std::string>("name") )
    {
      _name = name.get();
    }

    // loops
    if( boost::optional<unsigned int> loops = root.get_optional<unsigned int>("loops") )
    {
      _loops = loops.get();
    }

    // skip_first
    if( boost::optional<bool> skip_first = root.get_optional<bool>("skip_first") )
    {
      _skipFirst = skip_first.get();
    }

    // delay
    Delay defaultDelay = { DEFAULT_FRAME_NUMERATOR, DEFAULT_FRAME_DENOMINATOR };
    if( boost::optional<std::string> default_delay = root.get_optional<std::string>("default_delay") )
    {
      defaultDelay = str2delay(default_delay.get());
    }

    std::vector<Delay> delays;
    BOOST_FOREACH(const boost::property_tree::ptree::value_type& child, root.get_child("delays"))
    {
      delays.push_back(str2delay(child.second.data()));
    }

    // frames
    int delayIndex = 0;
    BOOST_FOREACH(const boost::property_tree::ptree::value_type& child, root.get_child("frames"))
    {
      std::string file;
      Delay delay;
      const boost::property_tree::ptree& frame = child.second;

      // filepath only.
      if(frame.empty())
      {
        file = frame.data();
        if(delayIndex < delays.size())
          delay = delays[delayIndex];
        else
          delay = defaultDelay;
      }

      // filepath and delay
      else
      {
        const boost::property_tree::ptree::value_type& value = frame.front();
        file = value.first;
        delay = str2delay(value.second.data());
      }

      // Add frame informations.
      const std::vector<std::string>& files = getFiles(file);
      const int count = files.size();
      for(int i = 0;  i < count;  ++i)
      {
        const FrameInfo frameInfo = { files[i], delay };
        _frameInfos.push_back(frameInfo);
      }

      ++delayIndex;
    }

    // Reset current directory.
    boost::filesystem::current_path(oldPath);
  }
  
  // Initialize XmlSpecReader object.
  XmlSpecReader::XmlSpecReader(const std::string& filePath)
  {
    
  }

} // namespace specreader
} // namespace apngasm
