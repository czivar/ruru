/* eslint-disable func-style */
/* eslint-disable no-console */
/* global console, process */
/* global document, window */
import 'babel-polyfill';

import React from 'react';
import ReactDOM from 'react-dom';
import {createStore} from 'redux';
import {Provider, connect} from 'react-redux';
import autobind from 'autobind-decorator';

import MapboxGLMap from 'react-map-gl';

import * as request from 'd3-request';
import DeckGL from 'deck.gl/react';
import {ArcLayer, ScreenGridLayer} from 'deck.gl';
import ViewportAnimation from './map-utils';

// ---- Default Settings ---- //
/* eslint-disable no-process-env */
const MAPBOX_ACCESS_TOKEN = process.env.MAPBOX_TOKEN;

const INITIAL_STATE = {
  mapViewState: {
    latitude: -1.32000000000005,
    longitude: 37.486055552412332,
    zoom: 1.7195968941767825,
    pitch: 26.57984356290917,
    bearing: -1.32000000000005
  },
  points: null,
  arcs: null,
  arcStrokeWidth: 3
};

const GOTO_NZ = {
  latitude: -43.97889477772071,
  longitude: 171.3568897450038,
  zoom: 1.7195968941767825,
  pitch: 26.57984356290917,
  bearing: -1.32000000000005
};

const LOOKAT_NZ = {
  latitude: -43.97889477772071,
  longitude: 131.3568897450038,
  zoom: 4.7195968941767825,
  pitch: 43.87968924799148,
  bearing: -9.688794326241123
}; 

const GOTO_EUROPE = {
  latitude: 49.97889477772071,
  longitude: 12.3568897450038,
  zoom: 2.7195968941767825,
  pitch: 2.57984356290917,
  bearing: -3.32000000000005
};

const GOTO_US = {
  latitude: 40.97889477772071,
  longitude: -98.3568897450038,
  zoom: 2.7195968941767825,
  pitch: 12.57984356290917,
  bearing: -15.32000000000005
};



// ---- Action ---- //
function addArc(points) {
  return {type: 'ADD_NEW_ARC', points};
}

function removeArcs() {
  return {type: 'REMOVE_ARCS'};
}

function updateMap(mapViewState) {
  //console.log(mapViewState);
  return {type: 'UPDATE_MAP', mapViewState};
}

function loadPoints(points) {
  return {type: 'LOAD_POINTS', points};
}

// Swaps data props when clicked to trigger WebGLBuffer updates
function swapData() {
  return {type: 'SWAP_DATA'};
}

// ---- Reducer ---- //
function reducer(state = INITIAL_STATE, action) {
  switch (action.type) {
    case 'ADD_NEW_ARC': {
      const arcs = action.points;
      const points = [];
      arcs.forEach(function(elem){
        points.push({position: [elem['targetPosition'][0], elem['targetPosition'][1]]});
      })
      return {...state, arcs: arcs, points: points};
    }
    case 'REMOVE_ARCS':
      return {...state, arcs: []};
    case 'UPDATE_MAP':
      return {...state, mapViewState: action.mapViewState};
    default:
      return state;
  }
}

// redux states -> react props
function mapStateToProps(state) {
  return {
    mapViewState: state.mapViewState,
    points: state.points,
    arcs: state.arcs
  };
}

const ArcLayerExample = props =>
  new ArcLayer({
    ...props,
    id: props.id || 'arcLayer',
    data: props.arcs,
    strokeWidth: props.arcStrokeWidth || 3,
    pickable: true,
    onHover: props.onArcHovered,
    onClick: props.onArcClicked
  });

const ScreenGridLayerExample = props =>
  new ScreenGridLayer({
    ...props,
    id: props.id || 'screenGridLayer',
    data: props.points,
    minColor: [0, 0, 0, 0],
    unitWidth: 2,
    unitHeight: 2
  });

// ---- View ---- //
class MapApp extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      hoverPoint: null,
      hoverArc: null,
      clickItem: null
    };
  }

  componentWillMount() {
    this._handleResize();
    window.addEventListener('resize', this._handleResize);
  }

  componentWillUnmount() {
    window.removeEventListener('resize', this._handleResize);
  }

  @autobind _updateArcStrokeWidth() {
    this.setState({arcStrokeWidth: 2});
  }

  @autobind _handleResize() {
    this.setState({width: window.innerWidth, height: window.innerHeight});
  }

  @autobind _handleViewportChanged(mapViewState) {
    if (mapViewState.pitch > 60) {
      mapViewState.pitch = 60;
    }
    this.props.dispatch(updateMap(mapViewState));
  }

  @autobind _handleArcHovered(info) {
    info.type = 'arc';
    this.setState({hoverArc: info});
  }

  @autobind _handleArcClicked(info) {
    info.type = 'arc';
    this.setState({clickItem: info});
  }

  @autobind _onChangeLayers(exampleName) {
    const {activeExamples} = this.state;
    activeExamples[exampleName] = !activeExamples[exampleName];
    this.setState({activeExamples});
  }

  @autobind _onWebGLInitialized(gl) {
    gl.enable(gl.DEPTH_TEST);
    gl.depthFunc(gl.LEQUAL);
  }

  _renderLayers() {
    const props = {
      ...this.props,
      ...this.state,
      onArcHovered: this._handleArcHovered,
      onArcClicked: this._handleArcClicked
    };
    const layers = [];

    let layer = ArcLayerExample
    let layerProps = props;
    /* eslint-disable max-depth */
    if (Array.isArray(layer)) {
      const makeProps = layer[1];
      layer = layer[0];
      layerProps = {
        ...props,
        ...makeProps(),
        id: 1
      };
    }
    layers.push(layer(layerProps));

    return layers;
  }

  _renderOverlay() {
    const {arcs, points, mapViewState} = this.props;
    const {width, height} = this.state;

    // wait until data is ready before rendering
    if (!arcs) {
      return [];
    }

    return (
      <DeckGL
        id="default-deckgl-overlay"
        width={width}
        height={height}
        debug
        {...mapViewState}
        onWebGLInitialized={ this._onWebGLInitialized }
        layers={this._renderLayers()}
      />
    );
  }

  render() {
    const {mapViewState} = this.props;
    const {width, height} = this.state;
    return (
      <MapboxGLMap
        mapboxApiAccessToken={MAPBOX_ACCESS_TOKEN}
        width={width}
        height={height}
        mapStyle={'mapbox://styles/mapbox/dark-v9'}
        perspectiveEnabled
        { ...mapViewState }
        onChangeViewport={this._handleViewportChanged}>
        { this._renderOverlay() }
      </MapboxGLMap>
    );
  }
}

const store = createStore(reducer);
const container = document.getElementById('map');
if (container !== null) {
  // ---- Main ---- //
  const App = connect(mapStateToProps)(MapApp);
  document.body.appendChild(container);
  ReactDOM.render(
    <Provider store={store}>
      <App />
    </Provider>,
    container
  );
}

// Generating red to green colors
function hslToRgb(h, s, l){
    var r, g, b;

    if(s == 0){
        r = g = b = l; // achromatic
    }else{
        function hue2rgb(p, q, t){
            if(t < 0) t += 1;
            if(t > 1) t -= 1;
            if(t < 1/6) return p + (q - p) * 6 * t;
            if(t < 1/2) return q;
            if(t < 2/3) return p + (q - p) * (2/3 - t) * 6;
            return p;
        }

        var q = l < 0.5 ? l * (1 + s) : l + s - l * s;
        var p = 2 * l - q;
        r = hue2rgb(p, q, h + 1/3);
        g = hue2rgb(p, q, h);
        b = hue2rgb(p, q, h - 1/3);
    }

    return [Math.floor(r * 255), Math.floor(g * 255), Math.floor(b * 255)];
}

function numberToColorHsl(i) {
    var hue = i * 1.2 / 360;
    var rgb = hslToRgb(hue, 1, .5);
    return [ rgb[1], rgb[0], rgb[2], 255 ]; 
}

// Array storing the arcs
const arcs = [];

function addNewArc(data, callback){
  if (arcs.length > 400){
    arcs.shift();
  }
  var color = Math.floor((parseInt(data['latency']) / 500) * 100);
  if (color > 100){
    color = 100;
  }

  arcs.push({
    sourcePosition: [
      data['source_long'],
      data['source_lat'],
      0.0
    ],
    targetPosition: [
      data['destination_long'],
      data['destination_lat'],
      0.0
    ],
    color: numberToColorHsl(color),
  });

}

function button_clear_measurements() {
  store.removeArcs();
}

// // Randomly generating events for testing (no socket is required)
// setInterval(function() {
//   const center = [
//     174.42694203247012,
//     -42.751537058389985
//   ];
//   const spread = 100;
//   addNewArc({
//     source_long: center[0] + (Math.random() - 0.5) * spread,
//     source_lat: center[1] + (Math.random() - 0.5) * spread,
//     destination_long: center[0] + (Math.random() - 0.5) * spread,
//     destination_lat: center[1] + (Math.random() - 0.5) * spread,
//     latency: 0
//   });

// }, 2000);

// Listen on the socket.io socket for new data
socket.on('latency', addNewArc);

ViewportAnimation.init();

function cameraUpdate(mapViewState){
  store.dispatch(updateMap(mapViewState));
}

var t0 = ViewportAnimation.fly(
    INITIAL_STATE['mapViewState'],
    GOTO_NZ,
    9000,
    cameraUpdate
  )
  .onComplete( function () {
    Object.keys(INITIAL_STATE['mapViewState']).forEach(key => {
      this[key] = INITIAL_STATE['mapViewState'][key];
    });
  })
  .easing(ViewportAnimation.Easing.Cubic.InOut);

var t1 = ViewportAnimation.fly(
    GOTO_NZ,
    LOOKAT_NZ,
    9000,
    cameraUpdate
  )
  .onComplete( function () {
    Object.keys(GOTO_NZ).forEach(key => {
      this[key] = GOTO_NZ[key];
    });
  })
  .easing(ViewportAnimation.Easing.Cubic.InOut);

var t2 = ViewportAnimation.fly(
    LOOKAT_NZ,
    GOTO_NZ,
    10000,
    cameraUpdate
  )
  .onComplete( function () {
    Object.keys(LOOKAT_NZ).forEach(key => {
      this[key] = LOOKAT_NZ[key];
    });
  })
  .easing(ViewportAnimation.Easing.Cubic.InOut);

var t3 = ViewportAnimation.fly(
    GOTO_NZ,
    INITIAL_STATE['mapViewState'],
    9000,
    cameraUpdate
  )
  .onComplete( function () {
     Object.keys(GOTO_NZ).forEach(key => {
      this[key] = GOTO_NZ[key];
    });
  })
  .easing(ViewportAnimation.Easing.Cubic.InOut);

var t4 = ViewportAnimation.fly(
    INITIAL_STATE['mapViewState'],
    GOTO_EUROPE,
    14000,
    cameraUpdate
  )
  .onComplete( function () {
    Object.keys(INITIAL_STATE['mapViewState']).forEach(key => {
      this[key] = INITIAL_STATE['mapViewState'][key];
    });
  })
  .easing(ViewportAnimation.Easing.Cubic.InOut);

var t5 = ViewportAnimation.fly(
    GOTO_EUROPE,
    INITIAL_STATE['mapViewState'],
    9000,
    cameraUpdate
  )
  .onComplete( function () {
      Object.keys(GOTO_EUROPE).forEach(key => {
      this[key] = GOTO_EUROPE[key];
    });
  })
  .easing(ViewportAnimation.Easing.Cubic.InOut);

var t6 = ViewportAnimation.fly(
    INITIAL_STATE['mapViewState'],
    GOTO_US,
    9000,
    cameraUpdate
  )
  .onComplete( function () {
      Object.keys(INITIAL_STATE['mapViewState']).forEach(key => {
      this[key] = INITIAL_STATE['mapViewState'][key];
    });
  })
  .easing(ViewportAnimation.Easing.Cubic.InOut);

var t7 = ViewportAnimation.fly(
    GOTO_US,
    INITIAL_STATE['mapViewState'],
    10000,
    cameraUpdate
  )
  .onComplete( function () {
      Object.keys(GOTO_US).forEach(key => {
      this[key] = GOTO_US[key];
    });
  })
  .easing(ViewportAnimation.Easing.Cubic.InOut);

t0.chain(t1);
t1.chain(t2);
t2.chain(t3);
t3.chain(t4);
t4.chain(t5);
t5.chain(t6);
t6.chain(t7);
t7.chain(t0);

window.startAnimation = function(){
  t0.start();
}

window.stopAnimation = function(){
  t0.stop();
}

// Call drawing every one second
setInterval(function(){
  // console.log('redraw map');
  store.dispatch(addArc(arcs.slice()));
}, 1000);
/* eslint-enable func-style */
/* eslint-enable no-console */
