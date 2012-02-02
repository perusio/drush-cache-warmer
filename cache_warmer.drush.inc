<?php
/**
 * @file   cache_warmer.drush.inc
 * @author António P. P. Almeida <appa@perusio.net>
 * @date   Tue Jan 10 21:44:07 2012
 *
 * @brief Implements a cache warmer issuing HTTP requests to a site following
 *        certain criteria.
 *
 */

// __DIR__ was introduced in PHP 5.3.0.
if (!defined('__DIR__')) {
  define('__DIR__', dirname(__FILE__));
}

// The default timeout in seconds for each request.
define('CACHE_WARMER_DEFAULT_TIMEOUT', 20);

// The link to the PHP documentation on the cURL extension.
define('CACHE_WARMER_CURL_PHP_LINK', 'http://php.net/manual/en/book.curl.php');

/**
 * Implements hook_drush_help().
 */
function cache_warmer_drush_help($section) {
  switch ($section) {
    case 'drush:cache-warmer':
      return dt('Keeps a cache warm by issuing HTTP requests to a site following certain criteria');
  }
} // cache_warmer_drush_help

/**
 * Implements hook_drush_command().
 */
function cache_warmer_drush_command() {
  $items = array();
  // The command options.
  $options = array(
    'latest-n' => 'Hit the URIs latest <n> content items: keep the cache primed with them',
    'updated-last' => 'Hit the URIS for the content items updated in the last <s> seconds (accepts strotime strings)',
    'hub-pages-file' => 'Path to a file containing a bunch of hub pages for a site',
    'timeout' =>
    sprintf("The timeout in seconds for each URI hit (default: %ds)", DRUSH_CACHE_WARMER_DEFAULT_TIMEOUT),
    'parallel' => 'The number of requests to issue in parallel (requires Nginx with Lua)',
    'crawler-service-uri' => 'The URI of the crawler web service',
    'no-aliases' => 'Do not use aliases when hitting the URIs',
  );
  // The provided commands.
  $items['cache-warmer'] = array(
    'callback' => 'cache_warmer_execute',
    'description' => 'Keeps an external cache primed by issuing HTTP requests according to several criteria.',
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_DATABASE,
    'arguments' => array('site' => 'Site base URI.'),
    'examples' => array(
      'drush cache-warmer --latest-n=300 --hub-pages-file=hub_pages.txt --timeout=5 http://example.com'
      => 'Hit the URIs for the 300 content items that where updated last, crawl the hub pages listed in hub_pages.txt for the example.com site with a timeout of 5 seconds for each request.',
      'drush cache-warmer --update-last="-4 hours" --timeout= --hub-pages-file=hpages.txt --timeout=10 http://foobar.com.'
      => 'Hit the URIs for the content items updated in the last 4 hours, crawl the hub pages listed in hub_pages.txt for the foobar.com site with a timeout of 10 seconds.',
    ),
    'options' => $options,
    'aliases' => array('cw'),
  );
  return $items;
} // cache_warmer_drush_command

/**
 * Validates the given base URI of the site to be crawled.
 *
 * @param $url string
 *   The base URI of the site to be crawled.
 * @return string or boolean
 *   The base URI or FALSE if there's an error.
 *
 */
function cache_warmer_validate_url($url = '') {
  // If the argument is missing we try to get from an alias.
  if (empty($url)) {
    // Try to get the site alias if given. We cannot use the '@self' alias
    // since it requires a full bootstrap.
    $alias = drush_get_context('alias');
    // If there's an alias given we try to get the base URI from the alias.
    if (array_key_exists('uri', $alias)) {
      $base_uri = $alias['uri'];
      drush_log(dt('Using base URI: @base-uri', array('@base-uri' => $base_uri)), 'warning');
    }
    else {
      return drush_set_error('CACHE_WARMER_NO_BASE_URI_FAIL',
                             dt('You must specify a base URI. Either through a site alias or explicitly'));
    }
  }
  else {
    $base_uri = $url;
  }
  // Parse the URL.
  $components = parse_url($base_uri);
  // Check if the URI is well formed.
  if ($components === FALSE) {
    return drush_set_error('CACHE_WARMER_URI_COMPONENTS_FAIL',
                           dt('Invalid base URI @uri given.', array('@uri' => $base_uri)));
  }
  // Get the URI scheme and verify it.
  $scheme = strtolower($components['scheme']);
  if ($scheme != 'http' && $scheme != 'https') {
    if (empty($scheme)) {
      return drush_set_error('CACHE_WARMER_EMPTY_SCHEME', dt('Invalid URI scheme given.'));
    }
    else {
      return drush_set_error('CACHE_WARMER_INVALID_SCHEME',
                             dt('Invalid URI scheme @scheme given.',
                                array('@scheme' => $scheme)), 'error');
    }
  }
  // Check if we have a query string.
  if (!empty($components['query'])) {
    return drush_set_error(
      'CACHE_WARMER_URL_QUERY_GIVEN',
      dt('Query string @qstring not allowed.',
         array('@qstring' => $components['query'])));
  }
  // Check if we have a fragment.
  if (!empty($components['fragment'])) {
    return drush_set_error('CACHE_WARMER_URL_FRAGMENT_GIVEN',
                           dt('Fragment @fragment not allowed.',
                              array('@fragment' => $components['fragment'])));
  }
  // Strip the trailing '/' if it exists.
  return trim($base_uri, '/');
} // cache_warmer_validate_url

/**
 * Check the arguments given to the drush command.
 *
 * @param $base_uri string
 *   The base URI.
 * @param $latest integer
 *   The number of the latest items to crawl.
 * @param $updated integer
 *   The elapsed time in seconds since the first item we want to crawl.
 * @param $hub_pages string
 *   The filename of the hub pages file.
 * @return string or boolean
 *   The base URI or FALSE if there's an error.
 */
function cache_warmer_check_arguments($base_uri = '', $latest = 0 , $updated = 0, $hub_pages) {
  // Validate the base URI given.
  $url_check = cache_warmer_validate_url($base_uri);
  if (!$url_check) {
    return $url_check;
  }

  if (!is_int($latest) || $latest < 0) {
    return drush_set_error('CACHE_WARMER_INVALID_LATEST',
                           dt('latest-n must be an integer greater than 0.'));
  }
  if (!is_int($updated ) || $updated < 0) {
    return drush_set_error(
      'CACHE_WARMER_INVALID_UPDATED',
      dt('updated-last must be the elapsed time in seconds since the first item we want to cache.')
    );
  }
  // Check if the hub pages file is readable.
  if (!empty($hub_pages) && (!is_readable($hub_pages) || filesize($hub_pages) == 0)) {
    return drush_set_error(
      'CACHE_WARMER_HUB_PAGES_FILE_FAIL',
      dt('Cannot open hub pages file @hub-file.',
         array('@hub-file' => $hub_pages)));
  }
  // Warn if both latest-n and are specified we'll user whichever returns the
  // greatest number of records.
  if ($latest > 0 && $updated > 0) {
    drush_log(
      dt('Both latest-n and updated last are specified. The one returning the most records will be used.'),
      'warning');
  }

  return $url_check;
} // cache_warmer_check_arguments

/**
 * Crawls the site using the given list of URIs using a single thread.
 *
 * @param $base_uri string
 *   The base URI of the site to be crawled.
 * @param $uris array
 *   The list of URIs to be crawled.
 * @param $timeout integer
 *   The timeout in seconds.
 *
 * @return array
 *   Array containing the status codes and request times for each crawled URI.
 *
 */
function cache_warmer_crawl_single($base_uri = '', $uris = array(), $hub_pages = '', $timeout) {

  $requests = array();

  $ch = curl_init();
  // cURL request basic options.
  curl_setopt_array($ch,
                    array(CURLOPT_NOBODY => TRUE, // HEAD request.
                          CURLOPT_TIMEOUT => $timeout,
                    ));
  // We first deal with the hub pages.
  if (!empty($hub_pages)) {
    $fp = fopen($hub_pages, 'r'); // get the handle
    if (!$fp) {
      drush_set_error(CACHE_WARMER_CANNOT_OPEN_HUBPAGES,
                      dt('Cannot open the hub pages file.'));
    }
    // Crawl the hub pages URIs.
    while (($line = fgets($fp)) !== FALSE) {
      $uri = trim($line); // remove white space on both ends
      // If the uri is '<front>' then it's a special case. The front page.
      $uri = $uri == '<front>' ? '' : $uri;
      // Create an object to store the request result.
      $request = new stdClass();
      $request->timestamp = $_SERVER['REQUEST_TIME'];
      curl_setopt($ch, CURLOPT_URL, $base_uri . '/' . $uri);
      curl_exec($ch);
      $request->status = curl_getinfo($ch, CURLINFO_HTTP_CODE);
      $request->time = curl_getinfo($ch, CURLINFO_TOTAL_TIME);
      $requests[$uri] = $request;
    }
    // Close the file handle.
    fclose($fp);
  }
  // Main loop. We store the total request time and status.
  foreach ($uris as $uri) {
    // Create an object to store the request result.
    $request = new stdClass();
    $request->timestamp = $_SERVER['REQUEST_TIME'];
    curl_setopt($ch, CURLOPT_URL, $base_uri . '/' . $uri);
    curl_exec($ch);
    $request->status = curl_getinfo($ch, CURLINFO_HTTP_CODE);
    $request->time = curl_getinfo($ch, CURLINFO_TOTAL_TIME);
    $requests[$uri] = $request;
  }
  // Release the cURL handler.
  curl_close($ch);

  return $requests;
} // cache_warmer_crawl_single

/**
 * Crawls the site using the given list of URIs using parallel requests.
 *
 * @param $base_uri string
 *   The base URI of the site to be crawled.
 * @param $uris array
 *   The list of URIs to be crawled.
 * @param $timeout integer
 *   The timeout in seconds.
 * @param $parallel string
 *   The number of requests to issue simultaneously.
 * @param $crawler_uri string
 *   The URI of the web service that implements the parallel crawl.
 * @return array
 *   Array containing the responses,
 *   status codes and request times for each crawled URI.
 *
 */
function cache_warmer_crawl_multiple($base_uri = '', $uris = array(), $hub_pages = '',
                                     $timeout, $parallel, $crawler_uri) {

  // Getting the number of URIs to be processed each time.
  $hub_pages_uris = explode("\n", file_get_contents($hub_pages));
  // Remove the last element. It's a '\n'.
  $temp = array_pop($hub_pages_uris); // temp var necessary for PHP :(
  $m = count($hub_pages_uris); // number of hub pages
  $n = count($uris); // number of URIs
  $rem = ($n + $m) % $parallel;
  $steps = ($n + $m - $rem) / $parallel; // integer division
  // Getting the timeout of each step. Multiply each request timeout by the
  // number of simultaneous requests.
  $step_timeout = $timeout * $steps;

  // Create a new array with shifted elements.
  $all_uris = array();
  // First the hub pages.
  for ($i = 0; $i < $m; $i++) {
    // The front page is a special case.
    $all_uris[$i] = $hub_pages_uris[$i] != '<front>' ? $hub_pages_uris[$i] : '';
  }

  // The other URIs after.
  $uris_keys = array_keys($uris);
  for ($i = 0; $i < $n; $i++) {
    $all_uris[$i + $m] = $uris[$uris_keys[$i]];
  }

  $ch = curl_init();
  // cURL request basic options.
  curl_setopt_array($ch,
                    array(CURLOPT_POST => TRUE, // POST request.
                          CURLOPT_TIMEOUT => $step_timeout,
                          CURLOPT_RETURNTRANSFER => TRUE,
                          CURLOPT_URL => $crawler_uri,
                    ));

  // Main loop posting the requests according to the given parallel processes.
  $post_data = array();
  $requests = array();
  for ($i = 0; $i < $steps; $i++) {
    // Fill in the POST data array.
    for ($j = 0; $j < $parallel; $j++) {
      $post_data["data$j"] = $all_uris[$j + ($i * $parallel)];
    }
    // Send the base URI as a specific field.
    $post_data['base_uri'] = $base_uri;
    // Create an object to store the request result.
    $request = new stdClass();
    $request->timestamp = $_SERVER['REQUEST_TIME'];
    // Make the POST request.
    curl_setopt($ch, CURLOPT_POSTFIELDS, http_build_query($post_data, '', '&'));
    $request->reply = curl_exec($ch);
    // Get the remainder of the request information.
    $request->status = curl_getinfo($ch, CURLINFO_HTTP_CODE);
    $request->time = curl_getinfo($ch, CURLINFO_TOTAL_TIME);
    $requests[$i] = $request;
  }

  // The remainder of the URIs to be hit.
  if ($rem > 0) {
    $post_data = array();
    for ($k = 0; $k < $rem; $k++) {
      $post_data["data$k"] = $all_uris[$k + $steps * $parallel];
    }
    // Send the base URI as a specific field.
    $post_data['base_uri'] = $base_uri;
    // Create an object to store the request result.
    $request = new stdClass();
    $request->timestamp = $_SERVER['REQUEST_TIME'];
    // Make the POST request.
    curl_setopt($ch, CURLOPT_POSTFIELDS, http_build_query($post_data, '', '&'));
    $request->reply = curl_exec($ch);
    // Get the remainder of the request information.
    $request->status = curl_getinfo($ch, CURLINFO_HTTP_CODE);
    $request->time = curl_getinfo($ch, CURLINFO_TOTAL_TIME);
    $requests[$i] = $request;
  } // if
  // Release the cURL handler.
  curl_close($ch);

  return $requests;
} // cache_warmer_crawl_multiple

/**
 * Crawl the URIs of the site specified starting at the given base URI.
 *
 * @param $base_uri string
 *   The base URI of the site being crawled.
 *
 * @return string
 *   The request responses, status and timeouts in JSON format.
 */
function cache_warmer_execute($base_uri = '') {

  // Check if the cURL extension is available. Bail out it not.
  if (!extension_loaded('curl')) {
    return drush_set_error('CACHE_WARMER_CURL_MISSING',
                           dt('The cURL PHP extension is required. See: @curl_uri',
                              array('@curl_uri' => CACHE_WARMER_CURL_PHP_LINK)));
  }
  // The n latest updated items.
  $latest_arg = drush_get_option('latest-n');
  $latest = is_numeric($latest_arg) ? (int) $latest_arg : 0;
  // The updated last argument can be the number of seconds or a string
  // accepted by strtotime.
  $updated_arg = drush_get_option('updated-last');
  if (is_numeric($updated_arg)) {
    $updated = $_SERVER['REQUEST_TIME'] - (int) $updated_arg;
  }
  elseif (is_string($updated_arg)) {
    $updated = (int) date('U', strtotime($updated_arg));
  }
  else {
    $updated = 0;
  }
  // The hub pages file.
  $hub_pages_arg = drush_get_option('hub-pages-file');
  $hub_pages = is_string($hub_pages_arg) ? $hub_pages_arg : '';
  // The timeout.
  $timeout_arg = drush_get_option('timeout');
  $timeout = is_numeric($timeout_arg) ? (int) $timeout_arg : CACHE_WARMER_DEFAULT_TIMEOUT;
  // The number of parallel "threads" to run.
  $parallel_arg = drush_get_option('parallel');
  $parallel = is_numeric($parallel_arg) ? (int) $parallel_arg : 0;
  // The URI of the parallel crawler web service.
  if (!empty($parallel)) {
    $crawler_service_uri = drush_get_option('crawler-service-uri');
    // Validate the URI.
    cache_warmer_validate_url($crawler_service_uri);
  }
  // Whether or not the site has aliases.
  $no_aliases = drush_get_option('no-aliases') ? TRUE : FALSE;
  // Trace the execution for debugging purposes.
  $trace = drush_get_option('trace') ? TRUE : FALSE;
  // Check the arguments.
  $base_url = cache_warmer_check_arguments($base_uri, $latest, $updated, $hub_pages);
  if (!$base_url) {
    drush_set_error(CACHE_WARMER_NO_BASE_URI_FAIL,
                    dt('Cannot determine base URI to be crawled.'));
  }
  // Print the arguments (debug).
  if ($trace) {
    drush_print('Arguments');
    drush_print_r(array('latest-n' => $latest,
                        'updated-last' => $updated,
                        'hub-pages-file' => $hub_pages,
                        'base-uri' => $base_url,
                        'timeout' => $timeout,
                        'parallel' => $parallel,
                        'crawler-service-uri' => $crawler_service_uri,
                        'trace' => $trace,
                  ));
  }
  // Getting the URIs to be hit. First we get the drupal major version.
  $items = array();
  $drupal_version = drush_drupal_major_version();

  switch ($drupal_version) {
    case '7':
      // Include the DB handling functions.
      if (!function_exists('cache_warmer_get_items_drupal7_both')) {
        require_once __DIR__ . '/includes/cache_warmer_db7.inc';
      }
      $items = cache_warmer_get_items_drupal7(_drush_sql_get_db_spec(), $latest, $updated, $no_aliases);
      break;
    case '6':
    case '5':
      // Include the DB handling functions.
      if (!function_exists('cache_warmer_get_items_drupal6_both')) {
        require_once __DIR__ . '/includes/cache_warmer_db6.inc';
      }
      $items = cache_warmer_get_items_drupal6(_drush_sql_get_db_spec(), $latest, $updated, $no_aliases);
      break;
    case FALSE:
      return drush_set_error('CACHE_WARMER_DRUPAL_VERSION_FAIL',
                             dt('Cannot determine the Drupal version.'));
    default:
      return drush_set_error('CACHE_WARMER_DRUPAL_VERSION_FAIL',
                             dt('Unsupported Drupal version.'));
  } // switch

  if ($trace) { // print the DB items: debug tracing
    drush_print('Items to be hit (DB)');
    drush_print_r($items);
  }

  // Crawling the given URIs.
  if ($parallel == 0) {
    // cURL invocation for single threaded mode.
    return json_encode(cache_warmer_crawl_single($base_url, $items, $hub_pages, $timeout)) . "\n";
  }
  else {
    // cURL invocation for parallel mode. (POST to Lua location.)
    return json_encode(cache_warmer_crawl_multiple($base_url, $items, $hub_pages,
                                                   $timeout, $parallel, $crawler_service_uri)) . "\n";
  }
} // cache_warmer_execute