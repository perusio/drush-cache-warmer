<?php
/**
 * @file   cache_warmer_db7.inc
 * @author António P. P. Almeida <appa@perusio.net>
 * @date   Thu Jan 26 21:19:44 2012
 *
 * @brief Functions for DB handling of the cache_warmer drush command for
 *        Drupal 7.
 *
 */

/**
 * Gets the array with the URIs of the items to be hit when both the latest n
 * and updated in the latest specified time for Drupal 7.
 *
 * @param $db_spec array
 *   The DB connection parameters.
 * @param $latest integer
 *   The number of the latest items to crawl.
 * @param $updated integer
 *   The elapsed time in seconds since the first item we want to crawl.
 * @return array
 *   The array of records that fit the criteria specified containing the URIs.
 */
function cache_warmer_get_items_drupal7_both($db_spec = NULL,
                                             $latest = 0,
                                             $updated = 0,
                                             $no_aliases = FALSE) {

  $items_latest = array();
  $items_updated = array();

  $items_latest = db_select('node', 'n')
    ->fields('n', array('nid'))
    ->orderBy('changed', 'DESC')
    ->range(0, $latest)
    ->execute()
    ->fetchAll();

  $items_updated = db_select('node', 'n')
    ->fields('n', array('nid'))
    ->condition('changed', $updated, '>=')
    ->orderBy('changed', 'DESC')
    ->execute()
    ->fetchAll();

  // Whichever is greater is used.
  $items = count($items_updated) > count($items_latest) ? $items_updated : $items_latest;
  // Get just the node URIs.
  $items_uris = array();
  foreach($items as $record) {
    $items_uris[] = 'node/' . $record->nid;
  }
  // If the site to be crawled has no aliases return now with the item list.
  if ($no_aliases) {
    return $items_uris;
  }
  // Get all the aliases. Return the result as a keyed array: 'source' =>
  // 'alias'.
  $pairs = db_select('url_alias', 'a')
    ->fields('a', array('source', 'alias'))
    ->condition('source', $items_uris, 'IN')
    ->execute()
    ->fetchAllKeyed();
  // Fill up the aliases array. If there's no alias return the original URI.
  $aliases = array();
  foreach($items_uris as $src) {
    $aliases[] = empty($pairs[$src]) ? $src : $pairs[$src];
  }

  return $aliases;
} // cache_warmer_get_items_drupal7_both

/**
 * Gets the array with the URIs of the items to be hit by the crawler for
 * Drupal 7.
 *
 * @param $db_spec array
 *   The DB connection parameters.
 * @param $latest integer
 *   The number of the latest items to crawl.
 * @param $updated integer
 *   The elapsed time in seconds since the first item we want to crawl.
 * @return array
 *   The array of records that fit the criteria specified containing the URIs.
 */
function cache_warmer_get_items_drupal7($db_spec = NULL, $latest = 0, $updated = 0, $no_aliases = FALSE) {

  // If both the latest and last n updated are given we perform the DB lookup
  // using subqueries.
  if ($latest > 0 && $updated > 0) {
    if ($no_aliases) {
      return cache_warmer_get_items_drupal7_both($db_spec, $latest, $updated, $no_aliases);
    }
    $result_aliases = cache_warmer_get_items_drupal7_both($db_spec, $latest, $updated, $no_aliases);
  }
  elseif ($updated > 0) {
    // If no aliases are used then just get the node IDs.
    if ($no_aliases) {
      $result = db_select('node', 'n')
        ->fields('n', array('nid'))
        ->condition('changed', $updated, '>=')
        ->orderBy('changed', 'DESC')
        ->execute()
        ->fetchAll();
    }
    else {
      // Use a JOIN there are aliases.
      $select = db_select('url_alias', 'a')
        ->fields('a', array('source', 'alias'))
        ->condition('changed', $updated, '>=');
      $pairs = $select->join('node', 'n', 'SUBSTR(a.src, 6) = n.nid')
        ->orderBy('changed', 'DESC')
        ->execute()
        ->fetchAllKeyed();
    }
  }
  else {
    if ($no_aliases) {
      // If no aliases are used then just get the node IDs.
      $result = db_select('node', 'n')
        ->fields('n', array('nid'))
        ->orderBy('changed', 'DESC')
        ->range(0, $latest)
        ->execute()
        ->fetchAll();
    }
    else {
      // Use a JOIN if there are aliases.
      $select = db_select('url_alias', 'a')->fields('a', array('source', 'alias'));
      $pairs = $select->join('node', 'n', 'SUBSTR(a.src, 6) = n.nid')
        ->orderBy('changed', 'DESC')
        ->range(0, $latest)
        ->execute()
        ->fetchAllKeyed();
    }
  }
  // If no aliases return node IDs in an array.
  if ($no_aliases) {
    $items_uris = array();
    // Fill up the array with the node IDs.
    while ($node_record = db_fetch_object($result)) {
      $items_uris[] = 'node/' . $node_record->nid;
    }
    return $items_uris;
  }

  return $pairs;
} // cache_warmer_get_items_drupal7