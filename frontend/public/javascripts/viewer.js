var destinations = [];

socket.on('counter', function(data){
  var sum = data['sum'];
  $('.connectionnumber').text(sum);
});

// Map stats (at the bottom of the page)
socket.on('mapstats', function(data){
  data.forEach(function(stats, index){
    source_html = "<tbody>"
    stats.forEach(function(elem){
      source_html += "<tr><td><img height='15px' src='/images/flags/"
      source_html += elem[1]['countrycode'].toLowerCase()
      source_html += "_64.png' /></td><td>"
      source_html += elem[0]
      source_html += "</td><td>"
      source_html += Math.round(elem[1]['mean'])
      source_html += " ms</td></tr>"
    });
    source_html += "</tbody>"
    $('#top5_table'+ index)
      .find('tbody')
      .replaceWith(source_html);
  });
});

// Refresh random data
socket.on('randomdata', function(data){
  $('.sourcecountry').html(data['source_country']+" <img width='25px' src='/images/flags/"+data['source_countrycode'].toLowerCase()+"_64.png'/>");
  $('.sourcecity').text(data['source_city']);
  $('.destinationcountry').html(data['destination_country']+" <img width='25px' src='/images/flags/"+data['destination_countrycode'].toLowerCase()+"_64.png'/>");
  $('.destinationcity').text(data['destination_city']);
  $('.latency').text(data['latency_total']+' ms');
});

function hello(){
  console.log('whaetver');
}
