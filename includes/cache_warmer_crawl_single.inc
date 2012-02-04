<?php
/**
 * @file   cache_warmer_crawl_single.inc
 * @author António P. P. Almeida <appa@perusio.net>
 * @date   Sat Feb  4 18:04:29 2012
 *
 * @brief  Provides the function crawling a site in single threaded mode for cache_warmer.
 *
 *
 */

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
