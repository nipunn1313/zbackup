// Copyright (c) 2012-2014 Konstantin Isakov <ikm@zbackup.org> and ZBackup contributors, see CONTRIBUTORS
// Part of ZBackup. Licensed under GNU GPLv2 or later + OpenSSL, see LICENSE

#include "config.hh"
#include "ex.hh"
#include "debug.hh"
#include "utils.hh"
#include "compression.hh"

#define VALID_SUFFIXES "Valid suffixes:\n" \
                       "B - multiply by 1 (bytes)\n" \
                       "KiB - multiply by 1024 (kibibytes)\n" \
                       "MiB - multiply by 1024*1024 (mebibytes)\n" \
                       "GiB - multiply by 1024*1024*1024 (gibibytes)\n" \
                       "KB - multiply by 1000 (kilobytes)\n" \
                       "MB - multiply by 1000*1000 (megabytes)\n" \
                       "GB - multiply by 1000*1000*1000 (gigabytes)\n" \

DEF_EX_STR( exInvalidThreadsValue, "Invalid threads value specified:", std::exception )

namespace ConfigHelper {
Config defaultConfig;

/* Textual representations of the tokens. */

static struct
{
  const char * name;
  const Config::OpCodes opcode;
  const Config::OptionType type;
  const char * description;
  const string defaultValue;
} keywords[] = {
  // Storable options
  {
    "chunk.max_size",
    Config::oChunk_max_size,
    Config::Storable,
    "Maximum chunk size used when storing chunks\n"
    "Affects deduplication ratio directly"
  },
  {
    "bundle.max_payload_size",
    Config::oBundle_max_payload_size,
    Config::Storable,
    "Maximum number of bytes a bundle can hold. Only real chunk bytes are\n"
    "counted, not metadata. Any bundle should be able to contain at least\n"
    "one arbitrary single chunk, so this should not be smaller than\n"
    "chunk.max_size"
  },
  {
    "bundle.compression_method",
    Config::oBundle_compression_method,
    Config::Storable,
    "Compression method for new bundles"
  },

  // Shortcuts for storable options
  {
    "compression",
    Config::oBundle_compression_method,
    Config::Storable,
    "Shortcut for bundle.compression_method"
  },

  // Runtime options
  {
    "threads",
    Config::oRuntime_threads,
    Config::Runtime,
    "Maximum number of compressor threads to use in backup process\n"
    "Default is %s on your system",
    Utils::numberToString( defaultConfig.runtime.threads )
  },
  {
    "cache-size",
    Config::oRuntime_cacheSize,
    Config::Runtime,
    "Cache size to use in restore process\n"
    "Affects restore process speed directly\n"
    VALID_SUFFIXES
    "Default is %sMiB",
    Utils::numberToString( defaultConfig.runtime.cacheSize / 1024 / 1024 )
  },
  {
    "exchange",
    Config::oRuntime_exchange,
    Config::Runtime,
    "Data to exchange between repositories in import/export process\n"
    "Can be specified multiple times\n"
    "Valid values:\n"
    "backups - exchange backup instructions (files in backups/ directory)\n"
    "bundles - exchange bundles with data (files in bunles/ directory)\n"
    "index - exchange indicies of chunks (files in index/ directory)\n"
    "No default value, you should specify it explicitly"
  },

  { NULL, Config::oBadOption, Config::None }
};

}

Config::Config()
{
  ConfigInfo * configInfo = new ConfigInfo;
  storable = configInfo;
  dPrintf( "Config is instantiated and initialized with default values\n" );
}

Config::Config( ConfigInfo * configInfo )
{
  storable = configInfo;
  dPrintf( "Config is instantiated and initialized with supplied ConfigInfo\n" );
}

Config::Config( const Config & configIn, ConfigInfo * configInfo )
{
  configInfo->MergeFrom( *configIn.storable );
  *this = configIn;
  storable = configInfo;
  dPrintf( "Config is instantiated and initialized with supplied values\n" );
}

Config::OpCodes Config::parseToken( const char * option, const OptionType type )
{
  for ( u_int i = 0; ConfigHelper::keywords[ i ].name; i++ )
  {
    if ( strcasecmp( option, ConfigHelper::keywords[ i ].name ) == 0 )
    {
      if ( ConfigHelper::keywords[ i ].type != type )
      {
        fprintf( stderr, "Invalid option type specified for %s\n", option );
        break;
      }

      return ConfigHelper::keywords[ i ].opcode;
    }
  }

  return Config::oBadOption;
}

bool Config::parseOption( const char * option, const OptionType type )
{
  string prefix;
  if ( type == Runtime )
    prefix.assign( "runtime" );
  else
  if ( type == Storable )
    prefix.assign( "storable" );
  dPrintf( "Parsing %s option \"%s\"...\n", prefix.c_str(), option );

  bool hasValue = false;
  size_t optionLength = strlen( option );
  char optionName[ optionLength ], optionValue[ optionLength ];

  if ( sscanf( option, "%[^=]=%s", optionName, optionValue ) == 2 )
  {
    dPrintf( "%s option name: %s, value: %s\n", prefix.c_str(),
        optionName, optionValue );
    hasValue = true;
  }
  else
    dPrintf( "%s option name: %s\n", prefix.c_str(), option );

  int opcode = parseToken( hasValue ? optionName : option, type );

  size_t sizeValue;
  char suffix[ 16 ];
  int n;
  unsigned int scale, scaleBase = 1;

  switch ( opcode )
  {
    case oBundle_compression_method:
      if ( !hasValue )
        return false;

      if ( strcmp( optionValue, "lzma" ) == 0 )
      {
        const_sptr< Compression::CompressionMethod > lzma =
          Compression::CompressionMethod::findCompression( "lzma" );
        if ( !lzma )
        {
          fprintf( stderr, "zbackup is compiled without LZMA support, but the code "
            "would support it. If you install liblzma (including development files) "
            "and recompile zbackup, you can use LZMA.\n" );
          return false;
        }
        Compression::CompressionMethod::selectedCompression = lzma;
      }
      else
      if ( strcmp( optionValue, "lzo1x_1" ) == 0 || strcmp( optionValue, "lzo" ) == 0 )
      {
        const_sptr< Compression::CompressionMethod > lzo =
          Compression::CompressionMethod::findCompression( "lzo1x_1" );
        if ( !lzo )
        {
          fprintf( stderr, "zbackup is compiled without LZO support, but the code "
            "would support it. If you install liblzo2 (including development files) "
            "and recompile zbackup, you can use LZO.\n" );
          return false;
        }
        Compression::CompressionMethod::selectedCompression = lzo;
      }
      else
      {
        fprintf( stderr, "zbackup doesn't support compression method '%s'. You may need a newer version.\n",
          optionValue );
        return false;
      }

      SET_STORABLE( bundle, compression_method,
          Compression::CompressionMethod::selectedCompression->getName() );
      dPrintf( "storable[bundle][compression_method] = %s\n", GET_STORABLE( bundle, compression_method ).c_str() );

      return true;
      /* NOTREACHED */
      break;

    case oRuntime_threads:
      if ( !hasValue )
        return false;

      sizeValue = runtime.threads;
      if ( sscanf( optionValue, "%zu %n", &sizeValue, &n ) != 1 ||
           optionValue[ n ] || sizeValue < 1 )
        throw exInvalidThreadsValue( optionValue );
      runtime.threads = sizeValue;

      dPrintf( "runtime[threads] = %zu\n", runtime.threads );

      return true;
      /* NOTREACHED */
      break;

    case oRuntime_cacheSize:
      if ( !hasValue )
        return false;

      sizeValue = runtime.cacheSize;
      if ( sscanf( optionValue, "%zu %15s %n",
                   &sizeValue, suffix, &n ) == 2 && !optionValue[ n ] )
      {
        // Check the suffix
        for ( char * c = suffix; *c; ++c )
          *c = tolower( *c );

        if ( strcmp( suffix, "b" ) == 0 )
        {
          scale = 1;
        }
        else
        if ( strcmp( suffix, "kib" ) == 0 )
        {
          scaleBase = 1024;
          scale = scaleBase;
        }
        else
        if ( strcmp( suffix, "mib" ) == 0 )
        {
          scaleBase = 1024;
          scale = scaleBase * scaleBase;
        }
        else
        if ( strcmp( suffix, "gib" ) == 0 )
        {
          scaleBase = 1024;
          scale = scaleBase * scaleBase * scaleBase;
        }
        else
        if ( strcmp( suffix, "kb" ) == 0 )
        {
          scaleBase = 1000;
          scale = scaleBase;
        }
        else
        if ( strcmp( suffix, "mb" ) == 0 )
        {
          scaleBase = 1000;
          scale = scaleBase * scaleBase;
        }
        else
        if ( strcmp( suffix, "gb" ) == 0 )
        {
          scaleBase = 1000;
          scale = scaleBase * scaleBase * scaleBase;
        }
        else
        {
          // SI or IEC
          fprintf( stderr, "Invalid suffix specified in cache size (%s): %s. "
                   VALID_SUFFIXES,
                   optionValue, suffix );
          return false;
        }
        runtime.cacheSize = sizeValue * scale;

        dPrintf( "runtime[cacheSize] = %zu\n", runtime.cacheSize );

        return true;
      }
      return false;
      /* NOTREACHED */
      break;

    case oRuntime_exchange:
      if ( !hasValue )
        return false;

      if ( strcmp( optionValue, "backups" ) == 0 )
        runtime.exchange.set( BackupExchanger::backups );
      else
      if ( strcmp( optionValue, "bundles" ) == 0 )
        runtime.exchange.set( BackupExchanger::bundles );
      else
      if ( strcmp( optionValue, "index" ) == 0 )
        runtime.exchange.set( BackupExchanger::index );
      else
      {
        fprintf( stderr, "Invalid exchange value specified: %s\n"
                 "Must be one of the following: backups, bundles, index\n",
                 optionValue );
        return false;
      }

      dPrintf( "runtime[exchange] = %s\n", runtime.exchange.to_string().c_str() );

      return true;
      /* NOTREACHED */
      break;

    case oBadOption:
    default:
      return false;
      /* NOTREACHED */
      break;
  }

  /* NOTREACHED */
  return false;
}

void Config::showHelp( const OptionType type )
{
  string prefix;
  if ( type == Runtime )
    prefix.assign( "runtime" );
  else
  if ( type == Storable )
    prefix.assign( "storable" );
  fprintf( stderr,
"Available %s options overview:\n\n"
"== help ==\n"
"shows this message\n"
"", prefix.c_str() );

  for ( u_int i = 0; ConfigHelper::keywords[ i ].name; i++ )
  {
    if ( ConfigHelper::keywords[ i ].type != type )
      continue;

    fprintf( stderr, "\n== %s ==\n", ConfigHelper::keywords[ i ].name );
    fprintf( stderr, ConfigHelper::keywords[ i ].description,
       ConfigHelper::keywords[ i ].defaultValue.c_str() );
    fprintf( stderr, "\n" );
  }
}

bool Config::parse( const string & str, google::protobuf::Message * mutable_message )
{
  return google::protobuf::TextFormat::ParseFromString( str, mutable_message );
}

string Config::toString( google::protobuf::Message const & message )
{
  std::string str;
  google::protobuf::TextFormat::PrintToString( message, &str );

  return str;
}

bool Config::validate( const string & configData, const string & newConfigData )
{
  ConfigInfo newConfig;
  return parse( newConfigData, &newConfig );
}

void Config::show()
{
  printf( "%s", toString( *storable ).c_str() );
}

void Config::show( const ConfigInfo & config )
{
  printf( "%s", toString( config ).c_str() );
}

bool Config::editInteractively( ZBackupBase * zbb )
{
  string configData( toString( *zbb->config.storable ) );
  string newConfigData( configData );

  if ( !zbb->spawnEditor( newConfigData, &validate ) )
    return false;
  ConfigInfo newConfig;
  if ( !parse( newConfigData, &newConfig ) )
    return false;
  if ( toString( *zbb->config.storable ) == toString( newConfig ) )
  {
    verbosePrintf( "No changes made to config\n" );
    return false;
  }

  verbosePrintf( "Updating configuration...\n" );

  zbb->config.storable->CopyFrom( newConfig );
  verbosePrintf(
"Configuration successfully updated!\n"
"Updated configuration:\n%s", toString( *zbb->config.storable ).c_str() );

  return true;
}
